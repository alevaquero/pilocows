#include "ble_gatt_server.h"
#include "session_storage.h"
#include "app_version.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "ble";

// ============================================================================
// GATT service — Pilocows scan-sync protocol (docs/ble-protocol.md).
// Real implementation over ESP-HOSTED: on CrowPanel (ESP32-P4) this NimBLE
// host stack talks to the ESP32-C6 co-processor's BLE radio transparently via
// ESP-HOSTED's virtual HCI transport (CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) —
// the code below is otherwise identical to the SC01's native-BLE version.
// The C6 must still be flashed with the matching ESP-HOSTED slave firmware
// for this to work on real hardware (see MIGRATION_PROGRESS.md).
// ============================================================================
//
// Service:         4C494C4F-434F-5753-0001-000000000000
// SESSION_LIST:    4C494C4F-434F-5753-0002-000000000000  (read — JSON session list, paged via CONTROL)
// DEVICE_STATUS:   4C494C4F-434F-5753-0003-000000000000  (read — JSON status)
// CONTROL:         4C494C4F-434F-5753-0004-000000000000  (write — JSON commands)
// SESSION_DATA:    4C494C4F-434F-5753-0005-000000000000  (read — JSON records for selected session)
// SESSION_META:    4C494C4F-434F-5753-0006-000000000000  (read — JSON metadata for selected session)
// AUDIO_INFO:      4C494C4F-434F-5753-0007-000000000000  (read — JSON {"exists":bool,"size":N} for selected audio source)
// AUDIO_DATA:      4C494C4F-434F-5753-0008-000000000000  (read — one raw binary page of the selected audio source)
//
// Audio pull: presence of AUDIO_INFO/AUDIO_DATA in service discovery is how
// the desktop distinguishes this firmware from the old SC01 Plus handheld
// (no recording hardware, no audio characteristics at all) — old devices
// still sync everything else normally, they just never see these two.

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
static const ble_uuid128_t UUID_AUDIO_INFO = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x07,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);
static const ble_uuid128_t UUID_AUDIO_DATA = BLE_UUID128_INIT(
    0x00,0x00,0x00,0x00, 0x00,0x00, 0x08,0x00,
    0x53,0x57, 0x4F,0x43, 0x4F,0x4C, 0x49,0x4C
);

static ble_command_callback_t s_cmd_callback = NULL;
static ble_sync_status_cb_t s_status_cb = NULL;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static struct ble_npl_callout s_adv_restart_co;
static struct ble_npl_callout s_adv_start_co;
static bool s_adv_enabled = false;
static uint16_t s_session_data_handle = 0;
static uint16_t s_session_meta_handle = 0;
static uint16_t s_status_handle = 0;
static uint32_t s_selected_session_id = 0; // set by CONTROL "select"
static uint32_t s_list_offset = 0;         // set by CONTROL "list_page"
static uint32_t s_data_offset = 0;         // set by CONTROL "data_page"

// Currently-open audio source (note or a specific tag's clip), selected via
// CONTROL "select_note_audio"/"select_tag_audio". One FILE* at a time, closed
// and reopened on every new selection and on connect/disconnect.
static FILE *s_audio_file = NULL;
static long s_audio_file_size = 0;
static uint32_t s_audio_offset = 0; // set by CONTROL "audio_page"

#define SESSION_LIST_PAGE_SIZE 6 // sessions per page — 6x~110 bytes = ~660 bytes, safely under 765
#define SESSION_DATA_PAGE_SIZE 5 // records per page  — 5x~120 bytes = ~600 bytes, safely under 765
#define AUDIO_DATA_PAGE_SIZE 480 // bytes per page — same safety margin as the JSON pages above

// Decodes the common BLE_HS_HCI_ERR(x) = 0x200+x disconnect reasons NimBLE
// reports, so a stall shows up in the log as a readable cause instead of a
// hex code that needs manual lookup.
static const char *disconnect_reason_str(int reason) {
    if (reason >= 0x200) {
        switch (reason - 0x200) {
            case 0x08: return "Connection Timeout (link-layer supervision timeout expired)";
            case 0x13: return "Remote User Terminated Connection";
            case 0x16: return "Local Host Terminated Connection";
            case 0x22: return "LMP Response Timeout";
            case 0x3d: return "Connection Failed to be Established";
            case 0x3e: return "Connection Failed to be Established (MIC failure or similar)";
            default: return "Unknown HCI error";
        }
    }
    return "Unknown (non-HCI) reason";
}

static void close_audio_file(void) {
    if (s_audio_file) {
        fclose(s_audio_file);
        s_audio_file = NULL;
    }
    s_audio_file_size = 0;
    s_audio_offset = 0;
}

static char s_status_json[128] = "{\"scan_count\":0,\"firmware_version\":\"" FIRMWARE_VERSION "\"}";

// Diagnostics: a periodic heartbeat while connected logs how long it's been
// since the last GATT request, so a log capture during a stall shows exactly
// how far the link got before it went quiet at the transport layer.
static esp_timer_handle_t s_heartbeat_timer = NULL;
static int64_t s_last_activity_us = 0;

static void touch_activity(const char *what) {
    s_last_activity_us = esp_timer_get_time();
    ESP_LOGI(TAG, "GATT activity: %s", what);
}

static void heartbeat_cb(void *arg) {
    (void)arg;
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    int64_t idle_ms = (esp_timer_get_time() - s_last_activity_us) / 1000;

    // RSSI of the last data channel PDU this side received from the
    // desktop — a device-side signal-strength reading independent of
    // whatever the desktop reports for its side of the same link, useful
    // for telling "weak/interfered signal" apart from other causes of a
    // slow or dropped transfer.
    int8_t rssi = 0;
    int rc = ble_gap_conn_rssi(s_conn_handle, &rssi);
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE heartbeat: connected (handle %d), %lldms since last GATT activity, rssi=%ddBm",
                 s_conn_handle, (long long)idle_ms, (int)rssi);
    } else {
        ESP_LOGI(TAG, "BLE heartbeat: connected (handle %d), %lldms since last GATT activity, rssi unavailable (rc=%d)",
                 s_conn_handle, (long long)idle_ms, rc);
    }
}

// Escape a C string for JSON output: quotes, backslashes, and control
// characters (0x00-0x1F) — multiline note fields can contain raw newlines,
// which are illegal unescaped inside a JSON string.
static int json_escape(char *dst, size_t dst_sz, const char *src, size_t src_max) {
    int pos = 0;
    for (size_t i = 0; i < src_max && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        int need = (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\t') ? 2
                 : (c < 0x20) ? 6 : 1;
        if ((size_t)pos + need + 1 >= dst_sz) break;

        if (c == '"' || c == '\\') {
            dst[pos++] = '\\'; dst[pos++] = (char)c;
        } else if (c == '\n') {
            dst[pos++] = '\\'; dst[pos++] = 'n';
        } else if (c == '\r') {
            dst[pos++] = '\\'; dst[pos++] = 'r';
        } else if (c == '\t') {
            dst[pos++] = '\\'; dst[pos++] = 't';
        } else if (c < 0x20) {
            pos += snprintf(dst + pos, 7, "\\u%04x", c);
        } else {
            dst[pos++] = (char)c;
        }
    }
    dst[pos] = '\0';
    return pos;
}

// SESSION_LIST: returns one page of sessions starting at s_list_offset.
// Desktop sets the page via CONTROL {"cmd":"list_page","offset":N} before each read.
// An empty array [] signals end-of-list.
static int gatt_session_list_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    touch_activity("SESSION_LIST read");

    if (s_status_cb) s_status_cb(BLE_SYNC_CONNECTED, 0, NULL);

    static session_meta_t sessions[MAX_SESSIONS];
    uint32_t total = session_list(sessions, MAX_SESSIONS);

    uint32_t start = s_list_offset;
    uint32_t end = start + SESSION_LIST_PAGE_SIZE;
    if (end > total) end = total;

    static char json_buf[800];
    int pos = 0;
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "[");
    for (uint32_t i = start; i < end; i++) {
        session_meta_t *m = &sessions[i];
        char esc_name[72];
        json_escape(esc_name, sizeof(esc_name), m->name, sizeof(m->name));
        pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
            "%s{\"id\":%" PRIu32 ",\"name\":\"%s\",\"type\":%u,"
            "\"count\":%" PRIu32 ",\"ts\":%ld,\"synced\":%d}",
            (i > start) ? "," : "",
            m->id, esc_name, (unsigned)m->type,
            m->tag_count, (long)m->created_at,
            m->synced ? 1 : 0);
    }
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "]");

    ESP_LOGI(TAG, "SESSION_LIST page offset=%" PRIu32 " -> %" PRIu32 "/%" PRIu32 " sessions (%d bytes)",
             s_list_offset, (end - start), total, pos);

    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)pos) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// SESSION_DATA: returns JSON array of tag records for the selected session.
// Only fields relevant to the session type are included. NimBLE handles Read
// Long automatically when data > MTU.
static int gatt_session_data_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    touch_activity("SESSION_DATA read");

    if (s_selected_session_id == 0) {
        return os_mbuf_append(ctxt->om, "[]", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    session_meta_t meta;
    if (session_get(s_selected_session_id, &meta) != ESP_OK) {
        return os_mbuf_append(ctxt->om, "[]", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (s_status_cb) {
        s_status_cb(BLE_SYNC_SESSION_READING, meta.id, meta.name);
    }

    static tag_record_t records[SESSION_DATA_PAGE_SIZE];
    uint32_t count = session_list_records_paged(s_selected_session_id, records,
                                                 SESSION_DATA_PAGE_SIZE, s_data_offset);

    static char json_buf[800];
    int pos = 0;
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "[");

    for (uint32_t i = 0; i < count; i++) {
        tag_record_t *r = &records[i];
        if (pos >= (int)sizeof(json_buf) - 256) break;

        char esc_eid[24], esc_note[300];
        json_escape(esc_eid, sizeof(esc_eid), r->eid, sizeof(r->eid));
        json_escape(esc_note, sizeof(esc_note), r->notes, sizeof(r->notes));

        pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
            "%s{\"eid\":\"%s\",\"ts\":%ld,\"type\":%u",
            i > 0 ? "," : "", esc_eid, (long)r->scanned_at, (unsigned)meta.type);

        switch (meta.type) {
        case SESSION_TYPE_WEIGHING:
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"weight_kg\":%u", (unsigned)r->weight_kg);
            break;
        case SESSION_TYPE_VACCINATION: {
            char esc_vax[300];
            json_escape(esc_vax, sizeof(esc_vax), r->vaccines, sizeof(r->vaccines));
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"vaccines\":\"%s\"", esc_vax);
            break;
        }
        case SESSION_TYPE_PREGNANCY: {
            const char *result;
            switch (r->pregnancy) {
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
        case SESSION_TYPE_TEST: {
            const char *result = r->test_result == TEST_POSITIVE ? "positive"
                               : r->test_result == TEST_NEGATIVE ? "negative"
                               : "inconclusive";
            char tname[TEST_NAME_MAX] = "";
            if (meta.test_id != 0) {
                test_get_name(meta.test_id, tname, sizeof(tname));
            }
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"test_result\":\"%s\",\"test_name\":\"%s\"", result, tname);
            break;
        }
        case SESSION_TYPE_REMOVAL: {
            char esc_reason[160];
            json_escape(esc_reason, sizeof(esc_reason), r->removal_reason, sizeof(r->removal_reason));
            pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
                ",\"removal_reason\":\"%s\"", esc_reason);
            break;
        }
        default: break;
        }

        pos += snprintf(json_buf + pos, sizeof(json_buf) - pos,
            ",\"note\":\"%s\",\"has_audio\":%d}", esc_note, r->has_audio ? 1 : 0);
    }
    pos += snprintf(json_buf + pos, sizeof(json_buf) - pos, "]");

    ESP_LOGI(TAG, "SESSION_DATA page offset=%" PRIu32 " -> %" PRIu32 " records (%d bytes)",
             s_data_offset, count, pos);

    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)pos) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int gatt_status_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    touch_activity("DEVICE_STATUS read");
    return os_mbuf_append(ctxt->om, s_status_json, strlen(s_status_json)) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// CONTROL: accepts JSON commands from the desktop. Supported commands:
//   {"cmd":"set_time","ts":"2026-04-20T10:00:00Z"}
//   {"cmd":"list_page","offset":0}   — set SESSION_LIST page window before next read
//   {"cmd":"select","id":1}          — select session for next SESSION_DATA read
//   {"cmd":"data_page","offset":0}   — set SESSION_DATA page window before next read
//   {"cmd":"mark_synced","id":1}     — mark session as synced
//   {"cmd":"delete","id":1}          — delete session from storage
//   {"cmd":"select_note_audio"}      — select the selected session's note audio for AUDIO_INFO/AUDIO_DATA
//   {"cmd":"select_tag_audio","eid":"..."} — select a tag's audio within the selected session
//   {"cmd":"audio_page","offset":0}  — set AUDIO_DATA page window before next read
static int gatt_control_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    touch_activity("CONTROL write");

    char buf[256] = {0};
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
    buf[len] = '\0';
    ESP_LOGI(TAG, "CONTROL: %s", buf);

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
            char iso[25] = {0};
            int k = 0;
            while (*ts && *ts != '"' && k < 24) iso[k++] = *ts++;
            s_cmd_callback(BLE_CMD_SET_TIME, iso);
        }

    } else if (strncmp(cmd_ptr, "select\"", 7) == 0) {
        const char *id_str = strstr(buf, "\"id\":");
        if (id_str) {
            s_selected_session_id = (uint32_t)atol(id_str + 5);
            s_data_offset = 0; // reset page on new session selection
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
                if (session_get(id, &m) == ESP_OK)
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

    } else if (strncmp(cmd_ptr, "select_note_audio\"", 18) == 0) {
        close_audio_file();
        if (s_selected_session_id != 0) {
            char path[64];
            session_note_audio_path(s_selected_session_id, path, sizeof(path));
            s_audio_file = fopen(path, "rb");
            if (s_audio_file) {
                fseek(s_audio_file, 0, SEEK_END);
                s_audio_file_size = ftell(s_audio_file);
                fseek(s_audio_file, 0, SEEK_SET);
            }
        }
        ESP_LOGI(TAG, "Selected note audio for session %" PRIu32 " (%ld bytes, exists=%d)",
                 s_selected_session_id, s_audio_file_size, s_audio_file != NULL);

    } else if (strncmp(cmd_ptr, "select_tag_audio\"", 17) == 0) {
        close_audio_file();
        const char *eid_str = strstr(buf, "\"eid\":\"");
        if (eid_str && s_selected_session_id != 0) {
            eid_str += 7;
            char eid[SESSION_EID_MAX + 1] = {0};
            int k = 0;
            while (*eid_str && *eid_str != '"' && k < SESSION_EID_MAX) eid[k++] = *eid_str++;

            // Linear scan for the record's audio_seq — sessions are small
            // enough (MAX_TAGS_PER_SESSION) that this is cheap, and it
            // mirrors the paged-read pattern SESSION_DATA already uses.
            // MUST be static: tag_record_t is large (~680 bytes, dominated by
            // its 256+128+256-byte string fields) and the NimBLE host task's
            // stack is only ~4KB total — 5 of them as a local array blew it
            // immediately on entry to this function (observed: stack
            // protection fault on every CONTROL write, regardless of which
            // command). Every other sizable buffer in this file is already
            // static for the same reason; this one was missed.
            static tag_record_t rec_buf[SESSION_DATA_PAGE_SIZE];
            uint32_t offset = 0;
            bool found = false, has_audio = false;
            uint16_t seq = 0;
            while (true) {
                uint32_t n = session_list_records_paged(s_selected_session_id, rec_buf, SESSION_DATA_PAGE_SIZE, offset);
                if (n == 0) break;
                for (uint32_t i = 0; i < n; i++) {
                    if (strcmp(rec_buf[i].eid, eid) == 0) {
                        found = true;
                        has_audio = rec_buf[i].has_audio;
                        seq = rec_buf[i].audio_seq;
                        break;
                    }
                }
                if (found) break;
                offset += n;
            }

            if (found && has_audio) {
                char path[64];
                session_tag_audio_path(s_selected_session_id, seq, path, sizeof(path));
                s_audio_file = fopen(path, "rb");
                if (s_audio_file) {
                    fseek(s_audio_file, 0, SEEK_END);
                    s_audio_file_size = ftell(s_audio_file);
                    fseek(s_audio_file, 0, SEEK_SET);
                }
            }
            ESP_LOGI(TAG, "Selected tag audio eid=%s found=%d has_audio=%d (%ld bytes)",
                     eid, found, has_audio, s_audio_file_size);
        }

    } else if (strncmp(cmd_ptr, "audio_page\"", 11) == 0) {
        const char *off_str = strstr(buf, "\"offset\":");
        if (off_str) {
            s_audio_offset = (uint32_t)atol(off_str + 9);
        }
    }
    return 0;
}

// SESSION_META: returns full JSON metadata for the selected session, including
// the session note and the device BT address as device_id.
static int gatt_session_meta_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    touch_activity("SESSION_META read");

    if (s_selected_session_id == 0) {
        return os_mbuf_append(ctxt->om, "{}", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    session_meta_t meta;
    if (session_get(s_selected_session_id, &meta) != ESP_OK) {
        return os_mbuf_append(ctxt->om, "{}", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

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
        "{\"id\":%" PRIu32 ",\"device_id\":\"%s\",\"name\":\"%s\","
        "\"type\":%u,\"created_at\":%ld,"
        "\"tag_count\":%" PRIu32 ",\"synced\":%d,\"note\":\"%s\",\"has_note_audio\":%d}",
        meta.id, device_id, esc_name,
        (unsigned)meta.type, (long)meta.created_at,
        meta.tag_count, meta.synced ? 1 : 0,
        esc_note, meta.has_note_audio ? 1 : 0);

    ESP_LOGI(TAG, "SESSION_META id=%" PRIu32 " (%d bytes)", meta.id, pos);

    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)pos) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// AUDIO_INFO: metadata for whatever audio source is currently selected via
// CONTROL "select_note_audio"/"select_tag_audio".
static int gatt_audio_info_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    touch_activity("AUDIO_INFO read");

    char json_buf[64];
    int pos = s_audio_file
        ? snprintf(json_buf, sizeof(json_buf), "{\"exists\":true,\"size\":%ld}", s_audio_file_size)
        : snprintf(json_buf, sizeof(json_buf), "{\"exists\":false,\"size\":0}");

    return os_mbuf_append(ctxt->om, json_buf, (uint16_t)pos) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// AUDIO_DATA: returns one raw binary page of the selected audio source,
// starting at s_audio_offset (set via CONTROL "audio_page" before each
// read). A short page (< AUDIO_DATA_PAGE_SIZE bytes) signals end-of-file,
// mirroring the SESSION_LIST/SESSION_DATA "short page ends the loop" pattern.
static int gatt_audio_data_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    touch_activity("AUDIO_DATA read");

    if (!s_audio_file) {
        return os_mbuf_append(ctxt->om, "", 0) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    static uint8_t page_buf[AUDIO_DATA_PAGE_SIZE];
    fseek(s_audio_file, (long)s_audio_offset, SEEK_SET);
    size_t n = fread(page_buf, 1, sizeof(page_buf), s_audio_file);

    // Always report a full, fixed-size page as long as there was any real
    // data at all — zero-pad the final short page out to AUDIO_DATA_PAGE_SIZE
    // instead of sending its true (shorter) length. A read whose length
    // differs from every other read in the sequence has been observed
    // confusing the BLE central's "Read Long" reassembly on macOS/
    // CoreBluetooth at this MTU, which silently hands back a stale earlier
    // page instead of the real final chunk. The client already knows the
    // true total size via AUDIO_INFO and trims the padding itself. The
    // cursor still advances by the REAL byte count (n), so the next read
    // naturally lands past EOF and reports a genuine empty page.
    size_t response_len = n;
    if (n > 0 && n < sizeof(page_buf)) {
        memset(page_buf + n, 0, sizeof(page_buf) - n);
        response_len = sizeof(page_buf);
    }

    ESP_LOGI(TAG, "AUDIO_DATA page offset=%" PRIu32 " -> %d bytes (%d real)",
             s_audio_offset, (int)response_len, (int)n);

    // Deliberately idempotent: this callback must NOT mutate s_audio_offset
    // itself. At this MTU, a 480-byte value exceeds the single-packet ATT
    // payload, so NimBLE's "Read Long" reassembly re-invokes this SAME
    // callback multiple times per one logical central-side read (mirroring
    // what SESSION_LIST already does harmlessly, since ITS callback never
    // mutates s_list_offset either). An earlier version of this function
    // self-advanced the offset on every invocation to remove the CONTROL
    // "audio_page" round trip per page — but that broke idempotency: each
    // extra NimBLE-internal re-invocation silently advanced the cursor by
    // another full page the central never actually saw, losing very close
    // to exactly half of every transfer (confirmed on-device: the file was
    // served completely and correctly, but the central only ever received
    // about half of it). The client must explicitly set the offset via
    // CONTROL "audio_page" before every single read again.
    return os_mbuf_append(ctxt->om, page_buf, (uint16_t)response_len) == 0
           ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SERVICE.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {   // SESSION_LIST
                .uuid = &UUID_SESSION_LIST.u,
                .access_cb = gatt_session_list_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {   // DEVICE_STATUS
                .uuid = &UUID_STATUS.u,
                .access_cb = gatt_status_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_status_handle,
            },
            {   // CONTROL
                .uuid = &UUID_CONTROL.u,
                .access_cb = gatt_control_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {   // SESSION_DATA
                .uuid = &UUID_SESSION_DATA.u,
                .access_cb = gatt_session_data_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_session_data_handle,
            },
            {   // SESSION_META
                .uuid = &UUID_SESSION_META.u,
                .access_cb = gatt_session_meta_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_session_meta_handle,
            },
            {   // AUDIO_INFO
                .uuid = &UUID_AUDIO_INFO.u,
                .access_cb = gatt_audio_info_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            {   // AUDIO_DATA
                .uuid = &UUID_AUDIO_DATA.u,
                .access_cb = gatt_audio_data_cb,
                .flags = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        },
    },
    { 0 }
};

static void start_advertising(void); // forward declaration

// Callout callback: runs on the NimBLE host event queue (correct context for
// NimBLE API calls) 300 ms after disconnect, giving the controller time to
// fully release the connection before advertising starts again.
static void adv_restart_co_cb(struct ble_npl_event *ev) {
    (void)ev;
    if (s_adv_enabled) start_advertising();
}

// Callout callback: dispatches a start_advertising() call to the NimBLE event
// queue, required when initiated from a non-NimBLE task (LVGL task).
static void adv_start_co_cb(struct ble_npl_event *ev) {
    (void)ev;
    if (s_adv_enabled) start_advertising();
}

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_selected_session_id = 0;
            s_list_offset = 0;
            s_data_offset = 0;
            close_audio_file();
            touch_activity("connect");
            if (s_heartbeat_timer) esp_timer_start_periodic(s_heartbeat_timer, 1000000);
            ESP_LOGI(TAG, "Desktop connected (handle %d)", s_conn_handle);
            if (s_status_cb) s_status_cb(BLE_SYNC_CONNECTED, 0, NULL);

            // Request a longer supervision timeout than whatever default got
            // negotiated. WiFi/BLE goes through ESP-Hosted to the ESP32-C6
            // over SDIO on this board (an extra RPC hop the SC01's native
            // BLE never had) — under host-side load a GATT response can take
            // longer to round-trip than a short supervision timeout
            // tolerates, and the link layer drops the connection outright
            // (observed: disconnect reason 0x208 = BLE_HS_HCI_ERR(conn
            // supervision timeout), ~1-3s after connecting).
            struct ble_gap_upd_params conn_params = {
                // Confirmed (not just diagnostic): tightening this to
                // itvl_min=12/itvl_max=24 for audio-transfer speed broke
                // SESSION_LIST pagination outright (hung after page 1 across
                // many repeated tests); reverting to 24/40 fixed it. Leave
                // this alone even though it's slower than we'd like for
                // audio — correctness over speed.
                .itvl_min = 24,             // 30ms (24 * 1.25ms)
                .itvl_max = 40,             // 50ms
                .latency = 0,
                // 20s (2000 * 10ms). Long BLE audio transfers (100+ seconds,
                // ~470 round trips) have been observed with per-page latency
                // that climbs steadily over the course of the transfer —
                // likely WiFi/BLE radio coexistence contention on the C6
                // co-processor, which also runs station WiFi + an OTA HTTP
                // server concurrently — occasionally exceeding the previous
                // 6s budget outright and killing the link (reason=0x208,
                // supervision timeout). This doesn't fix the underlying
                // slowdown, just gives a bad stretch enough room to recover
                // instead of dropping the connection.
                .supervision_timeout = 2000,
                .min_ce_len = 0,
                .max_ce_len = 0,
            };
            int upd_rc = ble_gap_update_params(s_conn_handle, &conn_params);
            if (upd_rc != 0) {
                ESP_LOGW(TAG, "ble_gap_update_params failed: rc=%d", upd_rc);
            }
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ESP_LOGW(TAG, "Connect attempt failed: status=%d - restarting advertising",
                     event->connect.status);
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_selected_session_id = 0;
        s_list_offset = 0;
        s_data_offset = 0;
        close_audio_file();
        if (s_heartbeat_timer) esp_timer_stop(s_heartbeat_timer); // harmless if not running
        ESP_LOGW(TAG, "Desktop disconnected: reason=0x%03x (%s) - restarting advertising in 300 ms",
                 event->disconnect.reason, disconnect_reason_str(event->disconnect.reason));
        if (s_status_cb) s_status_cb(BLE_SYNC_DISCONNECTED, 0, NULL);
        ble_npl_callout_reset(&s_adv_restart_co, ble_npl_time_ms_to_ticks32(300));
        break;
    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU updated: conn_handle=%d channel_id=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.channel_id, event->mtu.value);
        break;
    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc desc;
        if (event->conn_update.status == 0 &&
            ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
            ESP_LOGI(TAG, "Conn params updated: itvl=%dms latency=%d supervision_timeout=%dms",
                     desc.conn_itvl * 5 / 4, desc.conn_latency, desc.supervision_timeout * 10);
        } else {
            ESP_LOGW(TAG, "Conn param update failed: status=%d", event->conn_update.status);
        }
        break;
    }
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "Subscribe: attr_handle=%d reason=%d notify=%d indicate=%d",
                 event->subscribe.attr_handle, event->subscribe.reason,
                 event->subscribe.cur_notify, event->subscribe.cur_indicate);
        break;
    default:
        break;
    }
    return 0;
}

static void start_advertising(void) {
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"Pilocows";
    fields.name_len = 8;
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) ESP_LOGW(TAG, "ble_gap_adv_set_fields failed: rc=%d", rc);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event_cb, NULL);
    ESP_LOGI(TAG, "BLE advertising started");
}

static void ble_host_task(void *arg) {
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void ble_on_sync(void) {
    ble_hs_util_ensure_addr(0);
    // Do not auto-start advertising — screen_ble_sync calls ble_gatt_server_start_advertising().
    ESP_LOGI(TAG, "BLE stack ready - waiting for sync screen");
}

esp_err_t ble_gatt_server_init(ble_command_callback_t on_command) {
    s_cmd_callback = on_command;

    nimble_port_init();

    // Callouts must be initialised after nimble_port_init() so the host event queue exists.
    ble_npl_callout_init(&s_adv_restart_co, nimble_port_get_dflt_eventq(), adv_restart_co_cb, NULL);
    ble_npl_callout_init(&s_adv_start_co, nimble_port_get_dflt_eventq(), adv_start_co_cb, NULL);
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("Pilocows");

    ble_gatts_count_cfg(s_gatt_svcs);
    ble_gatts_add_svcs(s_gatt_svcs);

    ble_hs_cfg.sync_cb = ble_on_sync;
    nimble_port_freertos_init(ble_host_task);

    const esp_timer_create_args_t hb_args = {
        .callback = heartbeat_cb,
        .name = "ble_heartbeat",
    };
    esp_timer_create(&hb_args, &s_heartbeat_timer);

    ESP_LOGI(TAG, "BLE server initialized (sessions protocol)");
    return ESP_OK;
}

void ble_gatt_server_start_advertising(void) {
    s_adv_enabled = true;
    // Dispatch to the NimBLE event queue (this may be called from the LVGL task).
    ble_npl_callout_reset(&s_adv_start_co, ble_npl_time_ms_to_ticks32(10));
    ESP_LOGI(TAG, "BLE advertising requested");
}

void ble_gatt_server_stop_advertising(void) {
    s_adv_enabled = false;
    ble_npl_callout_stop(&s_adv_start_co);
    ble_npl_callout_stop(&s_adv_restart_co);
    ble_gap_adv_stop(); // returns error if not advertising — harmless
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ESP_LOGI(TAG, "BLE advertising stopped");
}

void ble_gatt_server_set_status_cb(ble_sync_status_cb_t cb) {
    s_status_cb = cb;
}

void ble_gatt_server_update_status(uint32_t scan_count, const char *firmware_version) {
    snprintf(s_status_json, sizeof(s_status_json),
             "{\"scan_count\":%" PRIu32 ",\"firmware_version\":\"%s\"}",
             scan_count, firmware_version ? firmware_version : "");
}

bool ble_gatt_server_is_connected(void) {
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

void ble_gatt_server_disconnect(void) {
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        // gap_event_cb will handle BLE_GAP_EVENT_DISCONNECT and restart advertising.
    }
}

esp_err_t ble_gatt_server_deinit(void) {
    ble_gatt_server_stop_advertising();
    s_cmd_callback = NULL;
    s_status_cb = NULL;
    ESP_LOGI(TAG, "BLE GATT server deinit");
    return ESP_OK;
}
