#include "session_storage.h"
#include "../i18n/i18n.h"
#include "../i18n/strings_en.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static const char *TAG = "sess_store";

// ── NVS keys ──────────────────────────────────────────────────────────────────
static const char *NVS_NS          = "pilocows";
static const char *NVS_ACTIVE_SESS = "active_sess";
static const char *NVS_NEXT_ID     = "next_sess_id";

// ── SPIFFS paths ──────────────────────────────────────────────────────────────
#define SPIFFS_BASE     "/spiffs"
#define SESS_IDX_PATH   "/spiffs/sess_idx.bin"
#define VACCINE_PATH    "/spiffs/vaccines.bin"
#define TEST_PATH       "/spiffs/tests.bin"

// ── Internal state ────────────────────────────────────────────────────────────
static bool              s_mounted      = false;
static uint32_t          s_active_id    = 0;       // 0 = no active session
static session_meta_t    s_active_cache;
static bool              s_cache_valid  = false;
static SemaphoreHandle_t s_mutex        = NULL;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

// Internal deletion marker stored in session_meta_t._deleted.
// 0 = normal session; anything else = tombstoned.
#define SESS_DELETED ((uint8_t)0x01)

// ─────────────────────────────────────────────────────────────────────────────
// NVS helpers
// ─────────────────────────────────────────────────────────────────────────────

static esp_err_t nvs_get_u32(const char *key, uint32_t *val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    err = nvs_get_u32(h, key, val);
    nvs_close(h);
    return err;
}

static esp_err_t nvs_set_u32(const char *key, uint32_t val)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_u32(h, key, val);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// ─────────────────────────────────────────────────────────────────────────────
// Session index helpers
// ─────────────────────────────────────────────────────────────────────────────

// Read one session_meta_t from the index file. id is 1-based.
static esp_err_t meta_read(uint32_t id, session_meta_t *out)
{
    FILE *f = fopen(SESS_IDX_PATH, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    long offset = (long)(id - 1) * sizeof(session_meta_t);
    if (fseek(f, offset, SEEK_SET) != 0) { fclose(f); return ESP_ERR_INVALID_ARG; }

    size_t n = fread(out, sizeof(session_meta_t), 1, f);
    fclose(f);
    return (n == 1) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// Write one session_meta_t to the index file at the slot for m->id.
static esp_err_t meta_write(const session_meta_t *m)
{
    // Open for read+write; create if not present
    FILE *f = fopen(SESS_IDX_PATH, "rb+");
    if (!f) f = fopen(SESS_IDX_PATH, "wb+");
    if (!f) return ESP_ERR_NO_MEM;

    long offset = (long)(m->id - 1) * sizeof(session_meta_t);
    if (fseek(f, offset, SEEK_SET) != 0) { fclose(f); return ESP_FAIL; }

    size_t n = fwrite(m, sizeof(session_meta_t), 1, f);
    fclose(f);
    return (n == 1) ? ESP_OK : ESP_FAIL;
}

// Persist the in-memory active cache back to disk.
static esp_err_t flush_active(void)
{
    if (!s_cache_valid || s_active_id == 0) return ESP_OK;
    return meta_write(&s_active_cache);
}

// Build the per-session record file path: /spiffs/s0001.bin
static void make_sess_path(uint32_t id, char *buf, size_t len)
{
    snprintf(buf, len, SPIFFS_BASE "/s%04" PRIu32 ".bin", id);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — init
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t session_storage_init(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_ERR_NO_MEM;
    }

    // Mount SPIFFS — tolerate already-mounted by an earlier init call
    if (!s_mounted) {
        esp_vfs_spiffs_conf_t conf = {
            .base_path       = SPIFFS_BASE,
            .partition_label = NULL,
            .max_files       = 10,
            .format_if_mount_failed = true,
        };
        esp_err_t err = esp_vfs_spiffs_register(&conf);
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "SPIFFS already mounted — reusing");
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
            return err;
        }
        s_mounted = true;
    }

    // Restore active session from NVS
    uint32_t saved_id = 0;
    if (nvs_get_u32(NVS_ACTIVE_SESS, &saved_id) == ESP_OK && saved_id != 0) {
        session_meta_t m;
        if (meta_read(saved_id, &m) == ESP_OK &&
            m.id == saved_id &&
            m._deleted == 0) {
            s_active_id  = saved_id;
            s_active_cache = m;
            s_cache_valid  = true;
            ESP_LOGI(TAG, "Restored active session %" PRIu32 " (%s)", saved_id, m.name);
        } else {
                // Session gone or closed — clear NVS pointer
            nvs_set_u32(NVS_ACTIVE_SESS, 0);
        }
    }

    return ESP_OK;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — session management
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t session_create(session_type_t type, const char *name,
                          const uint8_t *vax_ids, uint8_t vax_count,
                          uint8_t test_id,
                          uint32_t *out_id)
{
    LOCK();

    // Get and increment the next-id counter
    uint32_t new_id = 1;
    nvs_get_u32(NVS_NEXT_ID, &new_id);
    if (new_id == 0) new_id = 1;

    session_meta_t m;
    memset(&m, 0, sizeof(m));
    m.id         = new_id;
    m.type       = (uint8_t)type;
    m._deleted   = 0;
    m.created_at = time(NULL);
    m.tag_count  = 0;

    // Name
    if (name && name[0]) {
        strlcpy(m.name, name, sizeof(m.name));
    } else {
        session_build_default_name(type, m.name, sizeof(m.name));
    }

    // Vaccines
    if (type == SESSION_TYPE_VACCINATION && vax_ids && vax_count > 0) {
        m.vax_count = (vax_count <= SESSION_VAX_MAX) ? vax_count : SESSION_VAX_MAX;
        memcpy(m.vax_ids, vax_ids, m.vax_count);
    }

    // Test
    if (type == SESSION_TYPE_TEST) {
        m.test_id = test_id;
    }

    esp_err_t err = meta_write(&m);
    if (err != ESP_OK) { UNLOCK(); return err; }

    // Persist new counter
    nvs_set_u32(NVS_NEXT_ID, new_id + 1);

    // Make this the active session
    s_active_id    = new_id;
    s_active_cache = m;
    s_cache_valid  = true;
    nvs_set_u32(NVS_ACTIVE_SESS, new_id);

    ESP_LOGI(TAG, "Created session %" PRIu32 " \"%s\" type=%d", new_id, m.name, type);
    if (out_id) *out_id = new_id;

    UNLOCK();
    return ESP_OK;
}

esp_err_t session_set_active(uint32_t id)
{
    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(id, &m);
    if (err != ESP_OK)                          { UNLOCK(); return err; }
    if (m.id != id)                             { UNLOCK(); return ESP_ERR_NOT_FOUND; }
    if (m._deleted != 0)                        { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    s_active_id    = id;
    s_active_cache = m;
    s_cache_valid  = true;
    nvs_set_u32(NVS_ACTIVE_SESS, id);

    UNLOCK();
    return ESP_OK;
}

void session_clear_active(void)
{
    LOCK();
    flush_active();
    s_active_id   = 0;
    s_cache_valid = false;
    nvs_set_u32(NVS_ACTIVE_SESS, 0);
    UNLOCK();
}


bool session_get_active(session_meta_t *out)
{
    LOCK();
    if (!s_cache_valid || s_active_id == 0) { UNLOCK(); return false; }
    if (out) *out = s_active_cache;
    UNLOCK();
    return true;
}

int session_list_all(session_meta_t *out, int max_count)
{
    if (!out || max_count <= 0) return 0;
    if (max_count > 50) max_count = 50;

    LOCK();

    // Read all non-deleted sessions from the index file
    static session_meta_t buf[50];
    int total = 0;

    FILE *f = fopen(SESS_IDX_PATH, "rb");
    if (!f) { UNLOCK(); return 0; }

    session_meta_t m;
    while (fread(&m, sizeof(session_meta_t), 1, f) == 1) {
        if (m.id != 0 && m._deleted == 0) {
            if (total < 50) buf[total++] = m;
        }
    }
    fclose(f);

    // Sort newest first by id (ids are monotonically increasing)
    // Simple insertion sort — 50 max entries, trivial
    for (int i = 1; i < total; i++) {
        session_meta_t tmp = buf[i];
        int j = i - 1;
        while (j >= 0 && buf[j].id < tmp.id) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = tmp;
    }

    int n = (total < max_count) ? total : max_count;
    memcpy(out, buf, n * sizeof(session_meta_t));

    UNLOCK();
    return n;
}

esp_err_t session_delete(uint32_t id)
{
    LOCK();

    // If deleting the active session, clear active state first
    if (id == s_active_id) {
        s_active_id   = 0;
        s_cache_valid = false;
        nvs_set_u32(NVS_ACTIVE_SESS, 0);
    }

    session_meta_t m;
    esp_err_t err = meta_read(id, &m);
    if (err != ESP_OK || m.id != id) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    // Tombstone the meta record — keep m.id intact so meta_write can compute
    // the correct file offset; session_list_all already filters by _deleted.
    m._deleted = SESS_DELETED;
    err = meta_write(&m);

    // Delete the record file
    if (err == ESP_OK) {
        char path[32];
        make_sess_path(id, path, sizeof(path));
        remove(path);  // ignore error — file may not exist yet
    }

    UNLOCK();
    return err;
}

esp_err_t session_get_meta(uint32_t id, session_meta_t *out)
{
    if (!out || id == 0) return ESP_ERR_INVALID_ARG;
    LOCK();
    esp_err_t err = meta_read(id, out);
    if (err == ESP_OK && (out->id != id || out->_deleted != 0)) {
        err = ESP_ERR_NOT_FOUND;
    }
    UNLOCK();
    return err;
}

esp_err_t session_mark_synced(uint32_t id)
{
    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(id, &m);
    if (err != ESP_OK || m.id != id || m._deleted != 0) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }
    m.synced = 1;
    err = meta_write(&m);
    // Refresh cache if this is the active session
    if (err == ESP_OK && id == s_active_id && s_cache_valid) {
        s_active_cache.synced = 1;
    }
    UNLOCK();
    return err;
}

esp_err_t session_save_note(uint32_t id, const char *note)
{
    if (!note || id == 0) return ESP_ERR_INVALID_ARG;
    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(id, &m);
    if (err != ESP_OK || m.id != id || m._deleted != 0) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }
    strlcpy(m.note, note, sizeof(m.note));
    err = meta_write(&m);
    if (err == ESP_OK && id == s_active_id && s_cache_valid) {
        strlcpy(s_active_cache.note, note, sizeof(s_active_cache.note));
    }
    UNLOCK();
    return err;
}

int session_list_records(uint32_t session_id, tag_record_t *out, int max_count)
{
    if (!out || max_count <= 0 || session_id == 0) return 0;

    LOCK();
    char path[32];
    make_sess_path(session_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) { UNLOCK(); return 0; }

    int n = 0;
    while (n < max_count && fread(&out[n], sizeof(tag_record_t), 1, f) == 1) {
        n++;
    }
    fclose(f);
    UNLOCK();
    return n;
}

int session_list_records_paged(uint32_t session_id, tag_record_t *out,
                               int max_count, uint32_t offset)
{
    if (!out || max_count <= 0 || session_id == 0) return 0;

    LOCK();
    char path[32];
    make_sess_path(session_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) { UNLOCK(); return 0; }

    // Skip 'offset' records by seeking past them
    if (offset > 0) {
        fseek(f, (long)(offset * sizeof(tag_record_t)), SEEK_SET);
    }

    int n = 0;
    while (n < max_count && fread(&out[n], sizeof(tag_record_t), 1, f) == 1) {
        n++;
    }
    fclose(f);
    UNLOCK();
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — tag records
// ─────────────────────────────────────────────────────────────────────────────

esp_err_t session_save_record(const tag_record_t *rec)
{
    if (!rec) return ESP_ERR_INVALID_ARG;

    LOCK();

    if (!s_cache_valid || s_active_id == 0) { UNLOCK(); return ESP_ERR_INVALID_STATE; }

    char path[32];
    make_sess_path(s_active_id, path, sizeof(path));

    // Scan existing records for a matching EID
    FILE *f = fopen(path, "rb+");
    bool found = false;
    long match_offset = -1;

    if (f) {
        tag_record_t tmp;
        long offset = 0;
        while (fread(&tmp, sizeof(tag_record_t), 1, f) == 1) {
            if (strncmp(tmp.eid, rec->eid, SESSION_EID_MAX) == 0) {
                match_offset = offset;
                found = true;
                break;
            }
            offset += sizeof(tag_record_t);
        }
    }

    esp_err_t err;
    if (found && match_offset >= 0) {
        // Overwrite existing record (tag_count unchanged)
        fseek(f, match_offset, SEEK_SET);
        size_t n = fwrite(rec, sizeof(tag_record_t), 1, f);
        fclose(f);
        err = (n == 1) ? ESP_OK : ESP_FAIL;
    } else {
        // Append new record
        if (f) fclose(f);
        f = fopen(path, "ab");
        if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
        size_t n = fwrite(rec, sizeof(tag_record_t), 1, f);
        fclose(f);
        if (n == 1) {
            s_active_cache.tag_count++;
            err = meta_write(&s_active_cache);
        } else {
            err = ESP_FAIL;
        }
    }

    UNLOCK();
    return err;
}

bool session_find_record(const char *eid, tag_record_t *out)
{
    if (!eid) return false;

    LOCK();
    if (!s_cache_valid || s_active_id == 0) { UNLOCK(); return false; }

    char path[32];
    make_sess_path(s_active_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) { UNLOCK(); return false; }

    tag_record_t tmp;
    bool found = false;
    while (fread(&tmp, sizeof(tag_record_t), 1, f) == 1) {
        if (strncmp(tmp.eid, eid, SESSION_EID_MAX) == 0) {
            if (out) *out = tmp;
            found = true;
            break;
        }
    }
    fclose(f);

    UNLOCK();
    return found;
}

uint32_t session_get_tag_count(void)
{
    LOCK();
    uint32_t n = s_cache_valid ? s_active_cache.tag_count : 0;
    UNLOCK();
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — vaccine configuration
// ─────────────────────────────────────────────────────────────────────────────

int vaccine_list(vaccine_cfg_t *out, int max_count)
{
    if (!out || max_count <= 0) return 0;

    LOCK();
    FILE *f = fopen(VACCINE_PATH, "rb");
    if (!f) { UNLOCK(); return 0; }

    int n = 0;
    vaccine_cfg_t v;
    while (n < max_count && fread(&v, sizeof(vaccine_cfg_t), 1, f) == 1) {
        if (v.id != 0 && v.active) {
            out[n++] = v;
        }
    }
    fclose(f);
    UNLOCK();
    return n;
}

esp_err_t vaccine_add(const char *name, uint8_t *out_id)
{
    if (!name || !name[0]) return ESP_ERR_INVALID_ARG;

    LOCK();

    // Find max existing ID to assign next
    uint8_t max_id = 0;
    FILE *f = fopen(VACCINE_PATH, "rb");
    if (f) {
        vaccine_cfg_t v;
        while (fread(&v, sizeof(vaccine_cfg_t), 1, f) == 1) {
            if (v.id > max_id) max_id = v.id;
        }
        fclose(f);
    }

    if (max_id >= VACCINE_LIST_MAX) { UNLOCK(); return ESP_ERR_NO_MEM; }

    vaccine_cfg_t nv;
    memset(&nv, 0, sizeof(nv));
    nv.id     = max_id + 1;
    nv.active = 1;
    strlcpy(nv.name, name, sizeof(nv.name));

    f = fopen(VACCINE_PATH, "ab");
    if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
    size_t n = fwrite(&nv, sizeof(vaccine_cfg_t), 1, f);
    fclose(f);

    if (n == 1) {
        if (out_id) *out_id = nv.id;
        ESP_LOGI(TAG, "Added vaccine %d \"%s\"", nv.id, nv.name);
        UNLOCK();
        return ESP_OK;
    }
    UNLOCK();
    return ESP_FAIL;
}

esp_err_t vaccine_delete(uint8_t id)
{
    if (id == 0) return ESP_ERR_INVALID_ARG;

    LOCK();

    // Read all records, soft-delete the target, rewrite file
    static vaccine_cfg_t buf[VACCINE_LIST_MAX];
    int total = 0;
    bool found = false;

    FILE *f = fopen(VACCINE_PATH, "rb");
    if (f) {
        vaccine_cfg_t v;
        while (total < VACCINE_LIST_MAX && fread(&v, sizeof(vaccine_cfg_t), 1, f) == 1) {
            if (v.id == id) { v.active = 0; found = true; }
            buf[total++] = v;
        }
        fclose(f);
    }

    if (!found) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    f = fopen(VACCINE_PATH, "wb");
    if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
    fwrite(buf, sizeof(vaccine_cfg_t), total, f);
    fclose(f);

    UNLOCK();
    return ESP_OK;
}

bool vaccine_get_name(uint8_t id, char *out_name, size_t max_len)
{
    if (id == 0 || !out_name) return false;

    LOCK();
    FILE *f = fopen(VACCINE_PATH, "rb");
    if (!f) { UNLOCK(); return false; }

    vaccine_cfg_t v;
    bool found = false;
    while (fread(&v, sizeof(vaccine_cfg_t), 1, f) == 1) {
        if (v.id == id && v.active) {
            strlcpy(out_name, v.name, max_len);
            found = true;
            break;
        }
    }
    fclose(f);
    UNLOCK();
    return found;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API — test configuration (mirrors vaccine API, different path/limits)
// ─────────────────────────────────────────────────────────────────────────────

int test_list(test_cfg_t *out, int max_count)
{
    if (!out || max_count <= 0) return 0;

    LOCK();
    FILE *f = fopen(TEST_PATH, "rb");
    if (!f) { UNLOCK(); return 0; }

    int n = 0;
    test_cfg_t t;
    while (n < max_count && fread(&t, sizeof(test_cfg_t), 1, f) == 1) {
        if (t.id != 0 && t.active) {
            out[n++] = t;
        }
    }
    fclose(f);
    UNLOCK();
    return n;
}

esp_err_t test_add(const char *name, uint8_t *out_id)
{
    if (!name || !name[0]) return ESP_ERR_INVALID_ARG;

    LOCK();

    uint8_t max_id = 0;
    FILE *f = fopen(TEST_PATH, "rb");
    if (f) {
        test_cfg_t t;
        while (fread(&t, sizeof(test_cfg_t), 1, f) == 1) {
            if (t.id > max_id) max_id = t.id;
        }
        fclose(f);
    }

    if (max_id >= TEST_LIST_MAX) { UNLOCK(); return ESP_ERR_NO_MEM; }

    test_cfg_t nt;
    memset(&nt, 0, sizeof(nt));
    nt.id     = max_id + 1;
    nt.active = 1;
    strlcpy(nt.name, name, sizeof(nt.name));

    f = fopen(TEST_PATH, "ab");
    if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
    size_t n = fwrite(&nt, sizeof(test_cfg_t), 1, f);
    fclose(f);

    if (n == 1) {
        if (out_id) *out_id = nt.id;
        ESP_LOGI(TAG, "Added test %d \"%s\"", nt.id, nt.name);
        UNLOCK();
        return ESP_OK;
    }
    UNLOCK();
    return ESP_FAIL;
}

esp_err_t test_delete(uint8_t id)
{
    if (id == 0) return ESP_ERR_INVALID_ARG;

    LOCK();

    static test_cfg_t buf[TEST_LIST_MAX];
    int total = 0;
    bool found = false;

    FILE *f = fopen(TEST_PATH, "rb");
    if (f) {
        test_cfg_t t;
        while (total < TEST_LIST_MAX && fread(&t, sizeof(test_cfg_t), 1, f) == 1) {
            if (t.id == id) { t.active = 0; found = true; }
            buf[total++] = t;
        }
        fclose(f);
    }

    if (!found) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    f = fopen(TEST_PATH, "wb");
    if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
    fwrite(buf, sizeof(test_cfg_t), total, f);
    fclose(f);

    UNLOCK();
    return ESP_OK;
}

bool test_get_name(uint8_t id, char *out_name, size_t max_len)
{
    if (id == 0 || !out_name) return false;

    LOCK();
    FILE *f = fopen(TEST_PATH, "rb");
    if (!f) { UNLOCK(); return false; }

    test_cfg_t t;
    bool found = false;
    while (fread(&t, sizeof(test_cfg_t), 1, f) == 1) {
        if (t.id == id && t.active) {
            strlcpy(out_name, t.name, max_len);
            found = true;
            break;
        }
    }
    fclose(f);
    UNLOCK();
    return found;
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility
// ─────────────────────────────────────────────────────────────────────────────

void session_build_default_name(session_type_t type, char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    const char *type_str;
    switch (type) {
        case SESSION_TYPE_WEIGHING:    type_str = i18n_t(STR_EVENT_WEIGHING);    break;
        case SESSION_TYPE_VACCINATION: type_str = i18n_t(STR_EVENT_VACCINATION); break;
        case SESSION_TYPE_PREGNANCY:   type_str = i18n_t(STR_EVENT_PREGNANCY);   break;
        case SESSION_TYPE_TEST:        type_str = i18n_t(STR_EVENT_TEST);        break;
        case SESSION_TYPE_REMOVAL:     type_str = i18n_t(STR_EVENT_REMOVAL);     break;
        default:                       type_str = i18n_t(STR_EVENT_GENERAL);     break;
    }

    snprintf(buf, len, "%04d-%02d-%02d %s",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, type_str);
}
