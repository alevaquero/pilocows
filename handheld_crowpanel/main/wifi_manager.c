#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
#include <stdlib.h>

// NOTE: on CrowPanel (ESP32-P4) these are plain esp_wifi_* calls, same as on
// the SC01 (ESP32-S3, native WiFi) — esp_wifi_remote makes the ESP32-C6
// co-processor transparent to this code once ESP-HOSTED is actually bridging
// the two chips over SDIO (see MIGRATION_PROGRESS.md: the C6 also needs to be
// flashed with the ESP-HOSTED slave firmware, a separate physical step).

static const char *TAG = "wifi_mgr";
static const char *NVS_NS = "wifi_cfg";
static const char *NVS_SSID = "ssid";
static const char *NVS_PASS = "pass";

#define MAX_RETRY 8

static char s_ip_str[20] = "";
static bool s_connected = false;
static int s_retry = 0;

static wifi_on_connected_cb_t s_on_connected = NULL;
static wifi_on_error_cb_t s_on_error = NULL;
static wifi_on_scan_done_cb_t s_scan_cb = NULL;
static bool s_scanning = false;

static bool nvs_read_str(const char *key, char *out, size_t max_len) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = max_len;
    bool ok = (nvs_get_str(h, key, out, &len) == ESP_OK);
    nvs_close(h);
    return ok;
}

static void nvs_write_str(const char *key, const char *val) {
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, key, val);
    nvs_commit(h);
    nvs_close(h);
}

// Reads stored credentials and connects. Safe to call from the WiFi event handler.
static void try_connect_from_nvs(void) {
    char ssid[33] = {0}, pass[65] = {0};
    if (!nvs_read_str(NVS_SSID, ssid, sizeof(ssid)) || ssid[0] == '\0') return;
    nvs_read_str(NVS_PASS, pass, sizeof(pass));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = (pass[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
}

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg;

    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            try_connect_from_nvs();

        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            s_connected = false;
            s_ip_str[0] = '\0';
            wifi_event_sta_disconnected_t *evt = (wifi_event_sta_disconnected_t *)data;
            bool auth_fail = (evt->reason == WIFI_REASON_AUTH_FAIL ||
                               evt->reason == WIFI_REASON_AUTH_EXPIRE ||
                               evt->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT);
            if (!auth_fail && s_retry < MAX_RETRY) {
                s_retry++;
                ESP_LOGI(TAG, "Reconnecting (%d/%d) reason=%d", s_retry, MAX_RETRY, evt->reason);
                esp_wifi_connect();
            } else if (auth_fail) {
                ESP_LOGW(TAG, "Auth failed (reason=%d) - check password", evt->reason);
                if (s_on_error) s_on_error();
            } else {
                ESP_LOGW(TAG, "Max retries reached");
            }

        } else if (id == WIFI_EVENT_SCAN_DONE) {
            s_scanning = false;
            if (!s_scan_cb) return;

            uint16_t count = 0;
            esp_wifi_scan_get_ap_num(&count);
            if (count == 0) {
                wifi_on_scan_done_cb_t cb = s_scan_cb;
                s_scan_cb = NULL;
                cb(NULL, 0);
                return;
            }

            wifi_ap_record_t *recs = (wifi_ap_record_t *)malloc(count * sizeof(*recs));
            if (!recs) { s_scan_cb = NULL; return; }
            esp_wifi_scan_get_ap_records(&count, recs);

            wifi_ap_t *aps = (wifi_ap_t *)malloc(count * sizeof(*aps));
            if (!aps) { free(recs); s_scan_cb = NULL; return; }
            uint16_t n = 0;
            for (uint16_t i = 0; i < count; i++) {
                if (recs[i].ssid[0] == '\0') continue; // skip hidden SSIDs
                bool dup = false;
                for (uint16_t j = 0; j < n; j++) {
                    if (strcmp(aps[j].ssid, (char *)recs[i].ssid) == 0) { dup = true; break; }
                }
                if (!dup) {
                    strncpy(aps[n].ssid, (char *)recs[i].ssid, 32);
                    aps[n].ssid[32] = '\0';
                    aps[n].rssi = recs[i].rssi;
                    aps[n].open = (recs[i].authmode == WIFI_AUTH_OPEN);
                    n++;
                }
            }
            free(recs);

            wifi_on_scan_done_cb_t cb = s_scan_cb;
            s_scan_cb = NULL;
            cb(aps, n); // aps is valid only during this call
            free(aps);
        }

    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected = true;
        s_retry = 0;
        ESP_LOGI(TAG, "Connected - IP: %s", s_ip_str);
        if (s_on_connected) s_on_connected();
    }
}

void wifi_manager_init(void) {
    // Safe to call once — return INVALID_STATE if already done elsewhere
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return;
    }

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, NULL);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    ESP_LOGI(TAG, "WiFi manager ready");
}

void wifi_scan_start(wifi_on_scan_done_cb_t cb) {
    if (s_scanning) {
        ESP_LOGW(TAG, "Scan already in progress - ignoring");
        return;
    }
    s_scan_cb = cb;
    s_scanning = true;

    wifi_scan_config_t scan_cfg = {0};
    scan_cfg.show_hidden = false;
    scan_cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;

    esp_err_t err = esp_wifi_scan_start(&scan_cfg, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Scan start failed: %s", esp_err_to_name(err));
        s_scan_cb = NULL;
        s_scanning = false;
    }
}

void wifi_set_credentials(const char *ssid, const char *pass) {
    nvs_write_str(NVS_SSID, ssid ? ssid : "");
    nvs_write_str(NVS_PASS, pass ? pass : "");

    wifi_config_t cfg = {0};
    if (ssid) strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (pass) strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = (pass && pass[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    s_retry = 0;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_disconnect();
    esp_wifi_connect();
    ESP_LOGI(TAG, "Credentials updated - connecting to \"%s\"", ssid ? ssid : "");
}

bool wifi_is_connected(void) { return s_connected; }
const char *wifi_get_ip_str(void) { return s_ip_str; }
void wifi_set_on_connected(wifi_on_connected_cb_t cb) { s_on_connected = cb; }
void wifi_set_on_error(wifi_on_error_cb_t cb) { s_on_error = cb; }
