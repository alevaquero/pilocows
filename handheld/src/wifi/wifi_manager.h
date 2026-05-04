#pragma once
#include <stdbool.h>
#include <stdint.h>

// One AP entry returned by a scan — sorted by RSSI (strongest first).
typedef struct {
    char   ssid[33];   // null-terminated SSID
    int8_t rssi;
    bool   open;       // true = no password required
} wifi_ap_t;

typedef void (*wifi_on_connected_cb_t)(void);

// Callback invoked from the WiFi event task after a scan completes.
// |aps| points to a temporary heap array valid only during the call — copy what you need.
typedef void (*wifi_on_scan_done_cb_t)(const wifi_ap_t *aps, uint16_t count);

// Must be called after nvs_flash_init().
// Reads stored credentials and connects in the background if present.
void wifi_manager_init(void);

// Trigger an async WiFi scan. Ignored if a scan is already in progress.
void wifi_scan_start(wifi_on_scan_done_cb_t cb);

// Persist SSID + password to NVS and immediately attempt connection.
void wifi_set_credentials(const char *ssid, const char *pass);

bool        wifi_is_connected(void);
const char *wifi_get_ip_str(void);   // "192.168.x.x" or "" when not connected

// Register a callback fired once when an IP is obtained.
void wifi_set_on_connected(wifi_on_connected_cb_t cb);

// Register a callback fired when a connection attempt fails due to wrong credentials.
// Called from the WiFi event task — do not make LVGL calls inside.
typedef void (*wifi_on_error_cb_t)(void);
void wifi_set_on_error(wifi_on_error_cb_t cb);
