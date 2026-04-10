#include "ble_server.h"
#include "storage/scan_storage.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble";

// ---------------------------------------------------------------------------
// UUIDs — Pilocows GATT service
// See docs/ble-protocol.md
// ---------------------------------------------------------------------------
// Service:         4C494C4F-434F-5753-0001-000000000000
// SCAN_LIST:       4C494C4F-434F-5753-0002-000000000000
// DEVICE_STATUS:   4C494C4F-434F-5753-0003-000000000000
// CONTROL:         4C494C4F-434F-5753-0004-000000000000

// Shorter 16-bit aliases won't work with custom UUIDs — using full 128-bit
static const ble_uuid128_t UUID_SERVICE = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x01,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);
static const ble_uuid128_t UUID_SCAN_LIST = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x02,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);
static const ble_uuid128_t UUID_STATUS = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x03,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);
static const ble_uuid128_t UUID_CONTROL = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x04,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);

static ble_command_callback_t s_cmd_callback  = NULL;
static uint16_t               s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
static uint16_t               s_scan_list_handle = 0;
static uint16_t               s_status_handle    = 0;

// Status JSON buffer (updated by ble_server_update_status)
static char s_status_json[128] = "{\"scan_count\":0,\"firmware_version\":\"0.1.0\"}";

// ---------------------------------------------------------------------------
// GATT characteristic access callbacks
// ---------------------------------------------------------------------------
static int gatt_scan_list_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    // Build JSON from SPIFFS storage
    static char json_buf[8192];
    int len = scan_storage_to_json(json_buf, sizeof(json_buf));
    if (len < 0) {
        snprintf(json_buf, sizeof(json_buf), "[]");
        len = 2;
    }
    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)len) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_status_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    return os_mbuf_append(ctxt->om, s_status_json, strlen(s_status_json)) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_control_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    char buf[128] = {};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    buf[len] = '\0';

    ESP_LOGI(TAG, "CONTROL: %s", buf);

    if (strstr(buf, "clear_list") && s_cmd_callback) {
        s_cmd_callback(BLE_CMD_CLEAR_LIST, NULL);
    } else if (strstr(buf, "set_time") && s_cmd_callback) {
        // Extract ISO timestamp from JSON (simple string search)
        const char *iso = strstr(buf, "\"iso8601\":\"");
        if (iso) {
            iso += 11;
            char ts[25] = {};
            int i = 0;
            while (*iso && *iso != '"' && i < 24) ts[i++] = *iso++;
            s_cmd_callback(BLE_CMD_SET_TIME, ts);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// GATT service table
// ---------------------------------------------------------------------------
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SERVICE.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &UUID_SCAN_LIST.u,
                .access_cb  = gatt_scan_list_cb,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_scan_list_handle,
            },
            {
                .uuid       = &UUID_STATUS.u,
                .access_cb  = gatt_status_cb,
                .flags      = BLE_GATT_CHR_F_READ,
                .val_handle = &s_status_handle,
            },
            {
                .uuid       = &UUID_CONTROL.u,
                .access_cb  = gatt_control_cb,
                .flags      = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }  // Terminator
        },
    },
    { 0 }  // Terminator
};

// ---------------------------------------------------------------------------
// GAP event handler
// ---------------------------------------------------------------------------
static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Desktop connected (handle %d)", s_conn_handle);
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            // Restart advertising after failed connection
            ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                              NULL, gap_event_cb, NULL);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Desktop disconnected — restarting advertising");
        ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                          NULL, gap_event_cb, NULL);
        break;
    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {};
    fields.flags                  = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name                   = (uint8_t *)"Pilocows";
    fields.name_len               = 8;
    fields.name_is_complete       = 1;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, gap_event_cb, NULL);
    ESP_LOGI(TAG, "BLE advertising started");
}

static void ble_host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    start_advertising();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void ble_server_init(ble_command_callback_t on_command)
{
    s_cmd_callback = on_command;

    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Pilocows");

    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE server initialized");
}

void ble_server_update_status(uint32_t scan_count, const char *firmware_version)
{
    snprintf(s_status_json, sizeof(s_status_json),
             "{\"scan_count\":%lu,\"firmware_version\":\"%s\"}",
             (unsigned long)scan_count, firmware_version);
}

void ble_server_notify_scan_ready(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    // Send a notification with current scan count as a hint
    uint32_t count = scan_storage_count();
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"scan_count\":%lu}", (unsigned long)count);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));
    if (om) {
        ble_gatts_notify_custom(s_conn_handle, s_scan_list_handle, om);
    }
}

bool ble_server_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}
