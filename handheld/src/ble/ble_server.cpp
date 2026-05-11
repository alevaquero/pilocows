#include "ble_server.h"
#include "storage/session_storage.h"
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
#include <stdlib.h>

static const char *TAG = "ble";

// ---------------------------------------------------------------------------
// UUIDs — Pilocows GATT service  (docs/ble-protocol.md)
// ---------------------------------------------------------------------------
// Service:         4C494C4F-434F-5753-0001-000000000000
// SESSION_LIST:    4C494C4F-434F-5753-0002-000000000000  (read — JSON session list)
// DEVICE_STATUS:   4C494C4F-434F-5753-0003-000000000000  (read — JSON status)
// CONTROL:         4C494C4F-434F-5753-0004-000000000000  (write — JSON commands)
// SESSION_DATA:    4C494C4F-434F-5753-0005-000000000000  (read — JSON records for selected session)
// SESSION_META:    4C494C4F-434F-5753-0006-000000000000  (read — JSON metadata for selected session)

static const ble_uuid128_t UUID_SERVICE = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x01,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);
static const ble_uuid128_t UUID_SESSION_LIST = BLE_UUID128_INIT(
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
static const ble_uuid128_t UUID_SESSION_DATA = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x05,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);
static const ble_uuid128_t UUID_SESSION_META = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x06,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);

static ble_command_callback_t  s_cmd_callback        = NULL;
static ble_sync_status_cb_t    s_status_cb           = NULL;
static uint16_t                s_conn_handle          = BLE_HS_CONN_HANDLE_NONE;
static struct ble_npl_callout  s_adv_restart_co;
static struct ble_npl_callout  s_adv_start_co;
static bool                    s_adv_enabled          = false;
static uint16_t                s_session_list_handle  = 0;
static uint16_t                s_session_data_handle  = 0;
static uint16_t                s_session_meta_handle  = 0;
static uint16_t                s_status_handle        = 0;
static uint32_t                s_selected_session_id  = 0; // set by CONTROL "select"
static uint32_t                s_list_offset          = 0; // set by CONTROL "list_page"
static uint32_t                s_data_offset          = 0; // set by CONTROL "data_page"

#define SESSION_LIST_PAGE_SIZE  6   // sessions per page — 6×~110 bytes = ~660 bytes, safely under 765
#define SESSION_DATA_PAGE_SIZE  5   // records per page  — 5×~120 bytes = ~600 bytes, safely under 765

// Status JSON (updated by ble_server_update_status)
static char s_status_json[128] = "{\"scan_count\":0,\"firmware_version\":\"" FIRMWARE_VERSION "\"}";

// ---------------------------------------------------------------------------
// JSON builder helpers
// ---------------------------------------------------------------------------

// Escape a C string for JSON output (handles quotes and backslashes).
static int json_escape(char *dst, size_t dst_sz, const char *src, size_t src_max)
{
    int pos = 0;
    for (size_t i = 0; i < src_max && src[i] != '\0'; i++) {
        if ((size_t)pos + 3 >= dst_sz) break;
        char c = src[i];
        if (c == '"' || c == '\\') { dst[pos++] = '\\'; dst[pos++] = c; }
        else                       { dst[pos++] = c; }
    }
    dst[pos] = '\0';
    return pos;
}

// ---------------------------------------------------------------------------
// GATT characteristic access callbacks
// ---------------------------------------------------------------------------

// SESSION_LIST: returns one page of sessions starting at s_list_offset.
// Desktop sets the page via CONTROL {"cmd":"list_page","offset":N} before each read.
// An empty array [] signals end-of-list.
// Format: [{"id":1,"name":"2026-04-20 Weighing","type":1,"status":0,
//            "count":5,"ts":1745000000,"synced":0}, ...]
static int gatt_session_list_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    if (s_status_cb) s_status_cb(BLE_SYNC_CONNECTED, 0, NULL);

    static session_meta_t sessions[50];
    int total = session_list_all(sessions, 50);

    int start = (int)s_list_offset;
    int end   = start + SESSION_LIST_PAGE_SIZE;
    if (end > total) end = total;

    static char json_buf[800];
    int pos = 0;
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "[");
    for (int i = start; i < end; i++) {
        session_meta_t *m = &sessions[i];
        char esc_name[72];
        json_escape(esc_name, sizeof(esc_name), m->name, sizeof(m->name));
        pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
            "%s{\"id\":%lu,\"name\":\"%s\",\"type\":%u,"
            "\"count\":%lu,\"ts\":%ld,\"synced\":%d}",
            (i > start) ? "," : "",
            (unsigned long)m->id, esc_name,
            (unsigned)m->type,
            (unsigned long)m->tag_count, (long)m->created_at,
            m->synced ? 1 : 0);
    }
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "]");

    ESP_LOGI(TAG, "SESSION_LIST page offset=%lu → %d/%d sessions (%d bytes)",
             (unsigned long)s_list_offset, (end - start), total, pos);

    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)pos) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// SESSION_DATA: returns JSON array of tag records for the selected session.
// Format: [{"eid":"98200004321","ts":1745000000,"type":1,
//           "weight_kg":480,"vaccines":"IBR-BVD","pregnancy":"pregnant",
//           "tb_result":"negative","note":"paddock 3"}, ...]
//
// Only fields relevant to the session type are included.
// NimBLE handles Read Long automatically when data > MTU.
static int gatt_session_data_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    if (s_selected_session_id == 0) {
        return os_mbuf_append(ctxt->om, "[]", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    session_meta_t meta;
    if (session_get_meta(s_selected_session_id, &meta) != ESP_OK) {
        return os_mbuf_append(ctxt->om, "[]", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (s_status_cb) {
        s_status_cb(BLE_SYNC_SESSION_READING, meta.id, meta.name);
    }

    // Resolve vaccine names for vaccination sessions
    char vax_names[256] = {};
    if (meta.type == SESSION_TYPE_VACCINATION && meta.vax_count > 0) {
        int vpos = 0;
        for (int i = 0; i < meta.vax_count && i < SESSION_VAX_MAX; i++) {
            char vname[VACCINE_NAME_MAX + 1];
            if (vaccine_get_name(meta.vax_ids[i], vname, sizeof(vname))) {
                if (vpos > 0) vax_names[vpos++] = ',';
                int n = (int)strlen(vname);
                if (vpos + n < (int)sizeof(vax_names) - 1) {
                    memcpy(vax_names + vpos, vname, n);
                    vpos += n;
                }
            }
        }
        vax_names[vpos] = '\0';
    }

    // Read one page of records starting at s_data_offset
    static tag_record_t records[SESSION_DATA_PAGE_SIZE];
    int count = session_list_records_paged(s_selected_session_id, records,
                                           SESSION_DATA_PAGE_SIZE, s_data_offset);

    static char json_buf[800];
    int pos = 0;
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "[");

    for (int i = 0; i < count; i++) {
        tag_record_t *r = &records[i];
        if (pos >= (int)sizeof(json_buf) - 256) break;

        char esc_eid[24], esc_note[160];
        json_escape(esc_eid,  sizeof(esc_eid),  r->eid,  SESSION_EID_MAX);
        json_escape(esc_note, sizeof(esc_note),  r->note, SESSION_NOTE_MAX);

        // Base fields (i=0 within this page, but use comma logic correctly)
        pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
            "%s{\"eid\":\"%s\",\"ts\":%ld,\"type\":%u",
            i > 0 ? "," : "", esc_eid, (long)r->scanned_at, (unsigned)meta.type);

        // Type-specific fields
        switch ((session_type_t)meta.type) {
        case SESSION_TYPE_WEIGHING: {
            uint16_t w = 0;
            memcpy(&w, r->data, sizeof(uint16_t));
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"weight_kg\":%u", (unsigned)w);
            break;
        }
        case SESSION_TYPE_VACCINATION:
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"vaccines\":\"%s\"", vax_names);
            break;
        case SESSION_TYPE_PREGNANCY: {
            const char *result;
            switch ((pregnancy_result_t)r->data[0]) {
                case PREGNANCY_NO:       result = "not_pregnant";    break;
                case PREGNANCY_SMALL:    result = "small_pregnant";  break;
                case PREGNANCY_MEDIUM:   result = "medium_pregnant"; break;
                case PREGNANCY_BIG:      result = "big_pregnant";    break;
                case PREGNANCY_REJECTED: result = "rejected";        break;
                default:                 result = "unknown";         break;
            }
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"pregnancy\":\"%s\"", result);
            break;
        }
        case SESSION_TYPE_TB_TEST: {
            const char *result = r->data[0] == TB_POSITIVE  ? "positive"
                               : r->data[0] == TB_NEGATIVE  ? "negative"
                               : "inconclusive";
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"tb_result\":\"%s\"", result);
            break;
        }
        default: break;
        }

        pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
            ",\"note\":\"%s\"}", esc_note);
    }
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "]");

    ESP_LOGI(TAG, "SESSION_DATA page offset=%lu → %d records (%d bytes)",
             (unsigned long)s_data_offset, count, pos);

    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)pos) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_status_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    return os_mbuf_append(ctxt->om, s_status_json, strlen(s_status_json)) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// CONTROL: accepts JSON commands from the desktop.
// Supported commands:
//   {"cmd":"set_time","ts":"2026-04-20T10:00:00Z"}
//   {"cmd":"list_page","offset":0}   — set SESSION_LIST page window before next read
//   {"cmd":"select","id":1}          — select session for next SESSION_DATA read
//   {"cmd":"data_page","offset":0}   — set SESSION_DATA page window before next read
//   {"cmd":"mark_synced","id":1}     — mark session as synced
//   {"cmd":"delete","id":1}          — delete session from storage
static int gatt_control_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    char buf[256] = {};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    buf[len] = '\0';
    ESP_LOGI(TAG, "CONTROL: %s", buf);

    // Parse "cmd" field
    const char *cmd_ptr = strstr(buf, "\"cmd\":\"");
    if (!cmd_ptr) return 0;
    cmd_ptr += 7;

    if (strncmp(cmd_ptr, "list_page\"", 10) == 0) {
        const char *off_str = strstr(buf, "\"offset\":");
        if (off_str) {
            s_list_offset = (uint32_t)atol(off_str + 9);
            ESP_LOGI(TAG, "SESSION_LIST page offset set to %" PRIu32, s_list_offset);
        }

    } else if (strncmp(cmd_ptr, "set_time\"", 9) == 0) {
        const char *ts = strstr(buf, "\"ts\":\"");
        if (ts && s_cmd_callback) {
            ts += 6;
            char iso[25] = {};
            int k = 0;
            while (*ts && *ts != '"' && k < 24) iso[k++] = *ts++;
            s_cmd_callback(BLE_CMD_SET_TIME, iso);
        }

    } else if (strncmp(cmd_ptr, "select\"", 7) == 0) {
        const char *id_str = strstr(buf, "\"id\":");
        if (id_str) {
            s_selected_session_id = (uint32_t)atol(id_str + 5);
            s_data_offset = 0;  // reset page on new session selection
            ESP_LOGI(TAG, "Selected session %" PRIu32, s_selected_session_id);
        }

    } else if (strncmp(cmd_ptr, "data_page\"", 10) == 0) {
        const char *off_str = strstr(buf, "\"offset\":");
        if (off_str) {
            s_data_offset = (uint32_t)atol(off_str + 9);
            ESP_LOGI(TAG, "SESSION_DATA page offset set to %" PRIu32, s_data_offset);
        }

    } else if (strncmp(cmd_ptr, "mark_synced\"", 12) == 0) {
        const char *id_str = strstr(buf, "\"id\":");
        if (id_str) {
            uint32_t id = (uint32_t)atol(id_str + 5);
            session_mark_synced(id);
            ESP_LOGI(TAG, "Session %" PRIu32 " marked synced", id);
            if (s_status_cb) {
                session_meta_t m;
                if (session_get_meta(id, &m) == ESP_OK)
                    s_status_cb(BLE_SYNC_SESSION_DONE, id, m.name);
            }
        }

    } else if (strncmp(cmd_ptr, "delete\"", 7) == 0) {
        const char *id_str = strstr(buf, "\"id\":");
        if (id_str) {
            uint32_t id = (uint32_t)atol(id_str + 5);
            session_delete(id);
            ESP_LOGI(TAG, "Session %" PRIu32 " deleted by desktop", id);
        }
    }
    return 0;
}

// SESSION_META: returns full JSON metadata for the selected session, including
// the session note and the device BT address as device_id.
// The selected session is set via CONTROL {"cmd":"select","id":N} first.
// Format: {"id":1,"device_id":"AA:BB:CC:DD:EE:FF","name":"...","type":1,
//          "status":0,"created_at":1745000000,"tag_count":5,"synced":0,
//          "note":"..."}
static int gatt_session_meta_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

    if (s_selected_session_id == 0) {
        return os_mbuf_append(ctxt->om, "{}", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    session_meta_t meta;
    if (session_get_meta(s_selected_session_id, &meta) != ESP_OK) {
        return os_mbuf_append(ctxt->om, "{}", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    // Read device BT address
    char device_id[18] = "00:00:00:00:00:00";
    uint8_t own_addr_type;
    uint8_t addr[6];
    if (ble_hs_id_infer_auto(0, &own_addr_type) == 0 &&
        ble_hs_id_copy_addr(own_addr_type, addr, NULL) == 0) {
        snprintf(device_id, sizeof(device_id),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }

    static char json_buf[512];
    char esc_name[72], esc_note[160];
    json_escape(esc_name, sizeof(esc_name), meta.name, sizeof(meta.name));
    json_escape(esc_note, sizeof(esc_note), meta.note, sizeof(meta.note));

    int pos = snprintf(json_buf, sizeof(json_buf),
        "{\"id\":%lu,\"device_id\":\"%s\",\"name\":\"%s\","
        "\"type\":%u,\"created_at\":%ld,"
        "\"tag_count\":%lu,\"synced\":%d,\"note\":\"%s\"}",
        (unsigned long)meta.id, device_id, esc_name,
        (unsigned)meta.type, (long)meta.created_at,
        (unsigned long)meta.tag_count, meta.synced ? 1 : 0,
        esc_note);

    ESP_LOGI(TAG, "SESSION_META id=%lu (%d bytes)", (unsigned long)meta.id, pos);

    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)pos) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// ---------------------------------------------------------------------------
// GATT service table
// ---------------------------------------------------------------------------
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SERVICE.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   // SESSION_LIST
                .uuid       = &UUID_SESSION_LIST.u,
                .access_cb  = gatt_session_list_cb,
                .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_session_list_handle,
            },
            {   // DEVICE_STATUS
                .uuid       = &UUID_STATUS.u,
                .access_cb  = gatt_status_cb,
                .flags      = BLE_GATT_CHR_F_READ,
                .val_handle = &s_status_handle,
            },
            {   // CONTROL
                .uuid      = &UUID_CONTROL.u,
                .access_cb = gatt_control_cb,
                .flags     = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {   // SESSION_DATA
                .uuid       = &UUID_SESSION_DATA.u,
                .access_cb  = gatt_session_data_cb,
                .flags      = BLE_GATT_CHR_F_READ,
                .val_handle = &s_session_data_handle,
            },
            {   // SESSION_META
                .uuid       = &UUID_SESSION_META.u,
                .access_cb  = gatt_session_meta_cb,
                .flags      = BLE_GATT_CHR_F_READ,
                .val_handle = &s_session_meta_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

// ---------------------------------------------------------------------------
// GAP event handler
// ---------------------------------------------------------------------------
static void start_advertising(void);  // forward declaration

// Callout callback: runs on the NimBLE host event queue (correct context for
// NimBLE API calls) 300 ms after disconnect, giving the controller time to
// fully release the connection before advertising starts again.
static void adv_restart_co_cb(struct ble_npl_event *ev)
{
    (void)ev;
    if (s_adv_enabled) start_advertising();
}

// Callout callback: dispatches a start_advertising() call to the NimBLE event
// queue, which is required when initiated from a non-NimBLE task (LVGL task).
static void adv_start_co_cb(struct ble_npl_event *ev)
{
    (void)ev;
    if (s_adv_enabled) start_advertising();
}

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_selected_session_id = 0;
            s_list_offset = 0;
            s_data_offset = 0;
            ESP_LOGI(TAG, "Desktop connected (handle %d)", s_conn_handle);
            if (s_status_cb) s_status_cb(BLE_SYNC_CONNECTED, 0, NULL);
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_selected_session_id = 0;
        s_list_offset = 0;
        s_data_offset = 0;
        ESP_LOGI(TAG, "Desktop disconnected — restarting advertising in 300 ms");
        if (s_status_cb) s_status_cb(BLE_SYNC_DISCONNECTED, 0, NULL);
        // Delay advertising restart so the BLE controller fully releases the
        // connection before we begin advertising again.
        ble_npl_callout_reset(&s_adv_restart_co,
                              ble_npl_time_ms_to_ticks32(300));
        break;
    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {};
    fields.flags             = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name              = (uint8_t *)"Pilocows";
    fields.name_len          = 8;
    fields.name_is_complete  = 1;
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
    // Do not auto-start advertising — screen_ble_sync calls ble_server_start_advertising().
    ESP_LOGI(TAG, "BLE stack ready — waiting for sync screen");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void ble_server_init(ble_command_callback_t on_command)
{
    s_cmd_callback = on_command;

    nimble_port_init();

    // Callouts must be initialised after nimble_port_init() so the host
    // event queue exists.
    ble_npl_callout_init(&s_adv_restart_co,
                         nimble_port_get_dflt_eventq(),
                         adv_restart_co_cb,
                         NULL);
    ble_npl_callout_init(&s_adv_start_co,
                         nimble_port_get_dflt_eventq(),
                         adv_start_co_cb,
                         NULL);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Pilocows");

    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE server initialized (sessions protocol)");
}

void ble_server_start_advertising(void)
{
    s_adv_enabled = true;
    // Dispatch to the NimBLE event queue (this may be called from the LVGL task).
    ble_npl_callout_reset(&s_adv_start_co, ble_npl_time_ms_to_ticks32(10));
    ESP_LOGI(TAG, "BLE advertising requested");
}

void ble_server_stop_advertising(void)
{
    s_adv_enabled = false;
    // Cancel any pending start/restart callouts.
    ble_npl_callout_stop(&s_adv_start_co);
    ble_npl_callout_stop(&s_adv_restart_co);
    // Stop advertising if active, then disconnect any connected desktop.
    ble_gap_adv_stop();  // returns error if not advertising — harmless
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ESP_LOGI(TAG, "BLE advertising stopped");
}

void ble_server_set_status_cb(ble_sync_status_cb_t cb)
{
    s_status_cb = cb;
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
    uint32_t count = session_get_tag_count();
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"scan_count\":%lu}", (unsigned long)count);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, strlen(buf));
    if (om) ble_gatts_notify_custom(s_conn_handle, s_session_list_handle, om);
}

bool ble_server_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

void ble_server_disconnect(void)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        // gap_event_cb will handle BLE_GAP_EVENT_DISCONNECT and restart advertising.
    }
}
