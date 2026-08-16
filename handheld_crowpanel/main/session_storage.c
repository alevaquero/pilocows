#include "session_storage.h"
#include "i18n.h"
#include "strings_en.h"
#include "esp_spiffs.h"
#include "bsp_sd.h"
#include "audio_codec_util.h"
#include "soft_rtc.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>

static const char *TAG = "session_storage";
static const char *NVS_NAMESPACE = "pilocows";
static const char *KEY_ACTIVE_SESSION = "active_sess_id";
static const char *KEY_NEXT_ID = "next_sess_id";

#define SPIFFS_BASE   "/spiffs"
#define SESS_IDX_PATH "/spiffs/sess_idx.bin"
#define VACCINE_PATH  "/spiffs/vaccines.bin"
#define TEST_PATH     "/spiffs/tests.bin"

#define SD_AUDIO_BASE "/sdcard/audio"

static bool s_mounted = false;
static uint32_t s_active_session_id = 0;
static session_meta_t s_active_cache;
static bool s_cache_valid = false;
static SemaphoreHandle_t s_mutex = NULL;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

static void make_sess_path(uint32_t id, char *buf, size_t len) {
    snprintf(buf, len, SPIFFS_BASE "/s%04lu.tag", (unsigned long)id);
}

// ---- SD audio helpers ----

static void make_session_audio_dir(uint32_t session_id, char *buf, size_t len) {
    snprintf(buf, len, SD_AUDIO_BASE "/s%04lu", (unsigned long)session_id);
}

void session_note_audio_path(uint32_t session_id, char *buf, size_t len) {
    snprintf(buf, len, SD_AUDIO_BASE "/s%04lu/note.wav", (unsigned long)session_id);
}

void session_tag_audio_path(uint32_t session_id, uint16_t audio_seq, char *buf, size_t len) {
    snprintf(buf, len, SD_AUDIO_BASE "/s%04lu/t%03u.wav", (unsigned long)session_id, (unsigned)audio_seq);
}

// FAT (unlike SPIFFS) needs parent directories to exist before a file can be
// created inside them.
static void ensure_session_audio_dir(uint32_t session_id) {
    int base_err = mkdir(SD_AUDIO_BASE, 0775); // ignore error - fine if it already exists
    char dir[48];
    make_session_audio_dir(session_id, dir, sizeof(dir));
    int dir_err = mkdir(dir, 0775);
    ESP_LOGI(TAG, "ensure_session_audio_dir: mkdir(%s)=%d(errno=%d) mkdir(%s)=%d(errno=%d)",
             SD_AUDIO_BASE, base_err, base_err ? errno : 0, dir, dir_err, dir_err ? errno : 0);
}

// Removes every file in the session's audio directory, then the directory
// itself. Silently does nothing if the directory doesn't exist (SD absent,
// or the session never had any audio recorded).
static void remove_session_audio_dir(uint32_t session_id) {
    char dir[48];
    make_session_audio_dir(session_id, dir, sizeof(dir));
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *entry;
    char file_path[sizeof(dir) + 1 + sizeof(entry->d_name)];
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue; // skip "." / ".."
        snprintf(file_path, sizeof(file_path), "%s/%s", dir, entry->d_name);
        remove(file_path);
    }
    closedir(d);
    rmdir(dir);
}

// Standard 44-byte-header G.711 A-law WAV, 8kHz/8-bit mono
// (audio_format=WAVE_FORMAT_ALAW) — a real, standard WAV variant any
// correct reader can parse, roughly a quarter the size of a 16-bit/16kHz
// PCM capture. Recordings are captured at 16kHz/16-bit (the mic's native
// format, unchanged — see bsp_mic.h) and downsampled+companded down to
// this only at save time, via audio_codec_encode_alaw().
typedef struct __attribute__((packed)) {
    char riff[4]; uint32_t riff_size; char wave[4];
    char fmt[4]; uint32_t fmt_size; uint16_t audio_format; uint16_t num_channels;
    uint32_t sample_rate; uint32_t byte_rate; uint16_t block_align; uint16_t bits_per_sample;
    char data[4]; uint32_t data_size;
} wav_header_t;

#define WAVE_FORMAT_ALAW 6

static esp_err_t write_wav_file(const char *path, const int16_t *pcm, size_t n_samples) {
    uint8_t *alaw = NULL;
    size_t alaw_len = 0;
    esp_err_t enc_err = audio_codec_encode_alaw(pcm, n_samples, &alaw, &alaw_len);
    if (enc_err != ESP_OK) {
        ESP_LOGE(TAG, "write_wav_file(%s): A-law encode failed: %s", path, esp_err_to_name(enc_err));
        return enc_err;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "write_wav_file: fopen(%s) failed (errno=%d)", path, errno);
        free(alaw);
        return ESP_FAIL;
    }

    wav_header_t hdr = {
        .riff = {'R','I','F','F'}, .riff_size = 36 + (uint32_t)alaw_len, .wave = {'W','A','V','E'},
        .fmt = {'f','m','t',' '}, .fmt_size = 16, .audio_format = WAVE_FORMAT_ALAW, .num_channels = 1,
        .sample_rate = 8000, .byte_rate = 8000, .block_align = 1, .bits_per_sample = 8,
        .data = {'d','a','t','a'}, .data_size = (uint32_t)alaw_len,
    };
    size_t hn = fwrite(&hdr, sizeof(hdr), 1, f);
    size_t pn = fwrite(alaw, 1, alaw_len, f);
    int close_err = fclose(f);
    free(alaw);
    bool ok = (hn == 1 && pn == alaw_len && close_err == 0);
    ESP_LOGI(TAG, "write_wav_file(%s): %zu/%zu A-law bytes written (from %zu PCM samples), header=%zu/1, fclose=%d -> %s",
             path, pn, alaw_len, n_samples, hn, close_err, ok ? "OK" : "FAILED");
    return ok ? ESP_OK : ESP_FAIL;
}

// ---- session index (meta) helpers, id is 1-based ----

static esp_err_t meta_read(uint32_t id, session_meta_t *out) {
    if (id == 0) return ESP_ERR_INVALID_ARG;
    FILE *f = fopen(SESS_IDX_PATH, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;

    long offset = (long)(id - 1) * sizeof(session_meta_t);
    if (fseek(f, offset, SEEK_SET) != 0) { fclose(f); return ESP_ERR_INVALID_ARG; }

    size_t n = fread(out, sizeof(session_meta_t), 1, f);
    fclose(f);
    return (n == 1) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t meta_write(const session_meta_t *m) {
    FILE *f = fopen(SESS_IDX_PATH, "rb+");
    if (!f) f = fopen(SESS_IDX_PATH, "wb+");
    if (!f) return ESP_ERR_NO_MEM;

    long offset = (long)(m->id - 1) * sizeof(session_meta_t);
    if (fseek(f, offset, SEEK_SET) != 0) { fclose(f); return ESP_FAIL; }

    size_t n = fwrite(m, sizeof(session_meta_t), 1, f);
    fclose(f);
    return (n == 1) ? ESP_OK : ESP_FAIL;
}

void session_build_default_name(session_type_t type, char *buf, size_t len) {
    struct tm t;
    soft_rtc_get_local_tm(&t);

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

esp_err_t session_storage_init(void) {
    ESP_LOGI(TAG, "Initializing session storage");

    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) return ESP_ERR_NO_MEM;
    }

    if (!s_mounted) {
        esp_vfs_spiffs_conf_t conf = {
            .base_path = SPIFFS_BASE,
            .partition_label = "storage",
            .max_files = 10,
            .format_if_mount_failed = true,
        };
        esp_err_t err = esp_vfs_spiffs_register(&conf);
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "SPIFFS already mounted - reusing");
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
            return err;
        }
        s_mounted = true;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_OK) {
        uint32_t saved_id = 0;
        err = nvs_get_u32(handle, KEY_ACTIVE_SESSION, &saved_id);
        nvs_close(handle);

        if (saved_id != 0) {
            session_meta_t m;
            if (meta_read(saved_id, &m) == ESP_OK && m.id == saved_id && !m.deleted) {
                s_active_session_id = saved_id;
                s_active_cache = m;
                s_cache_valid = true;
                ESP_LOGI(TAG, "Restored active session %lu (%s)", (unsigned long)saved_id, m.name);
            }
        }
        err = ESP_OK;
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }

    return err;
}

uint32_t session_create(const char *name, session_type_t type,
                         const uint8_t *vax_ids, uint8_t vax_count, uint8_t test_id) {
    LOCK();

    uint32_t new_id = 1;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_u32(handle, KEY_NEXT_ID, &new_id);
        nvs_close(handle);
    }
    if (new_id == 0) new_id = 1;

    session_meta_t meta = {0};
    meta.id = new_id;
    meta.type = type;
    meta.tag_count = 0;
    meta.created_at = time(NULL);
    meta.updated_at = meta.created_at;

    if (name && name[0]) {
        strncpy(meta.name, name, sizeof(meta.name) - 1);
    } else {
        session_build_default_name(type, meta.name, sizeof(meta.name));
    }

    if (type == SESSION_TYPE_VACCINATION && vax_ids && vax_count > 0) {
        meta.vax_count = (vax_count <= sizeof(meta.vax_ids)) ? vax_count : sizeof(meta.vax_ids);
        memcpy(meta.vax_ids, vax_ids, meta.vax_count);
    }
    if (type == SESSION_TYPE_TEST) {
        meta.test_id = test_id;
    }

    esp_err_t err = meta_write(&meta);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create session: %s", esp_err_to_name(err));
        UNLOCK();
        return 0;
    }

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u32(handle, KEY_NEXT_ID, new_id + 1);
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Session created: %s (type %d, id %lu)", meta.name, type, (unsigned long)new_id);
    UNLOCK();
    return new_id;
}

bool session_get_active(session_meta_t *out) {
    LOCK();
    if (!s_cache_valid || s_active_session_id == 0) { UNLOCK(); return false; }
    if (out) *out = s_active_cache;
    UNLOCK();
    return true;
}

esp_err_t session_set_active(uint32_t session_id) {
    LOCK();

    if (session_id == 0) {
        s_active_session_id = 0;
        s_cache_valid = false;
    } else {
        session_meta_t m;
        esp_err_t err = meta_read(session_id, &m);
        if (err != ESP_OK || m.id != session_id || m.deleted) {
            UNLOCK();
            return ESP_ERR_NOT_FOUND;
        }
        s_active_session_id = session_id;
        s_active_cache = m;
        s_cache_valid = true;
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u32(handle, KEY_ACTIVE_SESSION, session_id);
        nvs_commit(handle);
        nvs_close(handle);
    }

    UNLOCK();
    return ESP_OK;
}

esp_err_t session_get(uint32_t id, session_meta_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    LOCK();
    esp_err_t err = meta_read(id, out);
    if (err == ESP_OK && (out->id != id || out->deleted)) {
        err = ESP_ERR_NOT_FOUND;
    }
    UNLOCK();
    return err;
}

uint32_t session_list(session_meta_t *out, uint32_t max_out) {
    if (!out || max_out == 0) return 0;
    if (max_out > MAX_SESSIONS) max_out = MAX_SESSIONS;

    LOCK();

    static session_meta_t buf[MAX_SESSIONS];
    uint32_t total = 0;

    FILE *f = fopen(SESS_IDX_PATH, "rb");
    if (!f) { UNLOCK(); return 0; }

    session_meta_t m;
    while (total < MAX_SESSIONS && fread(&m, sizeof(session_meta_t), 1, f) == 1) {
        if (m.id != 0 && !m.deleted) {
            buf[total++] = m;
        }
    }
    fclose(f);

    // Newest first (ids are sequential/monotonic) — insertion sort, small N
    for (uint32_t i = 1; i < total; i++) {
        session_meta_t tmp = buf[i];
        int j = (int)i - 1;
        while (j >= 0 && buf[j].id < tmp.id) {
            buf[j + 1] = buf[j];
            j--;
        }
        buf[j + 1] = tmp;
    }

    uint32_t n = (total < max_out) ? total : max_out;
    memcpy(out, buf, n * sizeof(session_meta_t));

    UNLOCK();
    return n;
}

esp_err_t session_close(uint32_t session_id) {
    LOCK();
    if (s_active_session_id == session_id) {
        s_active_session_id = 0;
        s_cache_valid = false;
    }
    UNLOCK();
    return ESP_OK;
}

esp_err_t session_delete(uint32_t session_id) {
    LOCK();

    session_meta_t m;
    esp_err_t err = meta_read(session_id, &m);
    if (err != ESP_OK || m.id != session_id) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    m.deleted = 1;
    err = meta_write(&m);

    if (err == ESP_OK) {
        char path[32];
        make_sess_path(session_id, path, sizeof(path));
        remove(path);
        remove_session_audio_dir(session_id); // cascade-delete session/tag voice notes
    }

    if (s_active_session_id == session_id) {
        s_active_session_id = 0;
        s_cache_valid = false;
        nvs_handle_t handle;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
            nvs_set_u32(handle, KEY_ACTIVE_SESSION, 0);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }

    UNLOCK();
    return err;
}

esp_err_t session_save_note(uint32_t session_id, const char *note) {
    if (!note || session_id == 0) return ESP_ERR_INVALID_ARG;

    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(session_id, &m);
    if (err != ESP_OK || m.id != session_id || m.deleted) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(m.note, note, sizeof(m.note) - 1);
    err = meta_write(&m);

    if (err == ESP_OK && session_id == s_active_session_id && s_cache_valid) {
        strncpy(s_active_cache.note, note, sizeof(s_active_cache.note) - 1);
    }

    UNLOCK();
    return err;
}

esp_err_t session_update_name(uint32_t session_id, const char *name) {
    if (!name || !name[0] || session_id == 0) return ESP_ERR_INVALID_ARG;

    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(session_id, &m);
    if (err != ESP_OK || m.id != session_id || m.deleted) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(m.name, name, sizeof(m.name) - 1);
    m.name[sizeof(m.name) - 1] = '\0';
    err = meta_write(&m);

    if (err == ESP_OK && session_id == s_active_session_id && s_cache_valid) {
        strncpy(s_active_cache.name, name, sizeof(s_active_cache.name) - 1);
        s_active_cache.name[sizeof(s_active_cache.name) - 1] = '\0';
    }

    UNLOCK();
    return err;
}

// ===== Audio notes (SD card) =====

esp_err_t session_save_note_audio(uint32_t session_id, const int16_t *pcm, size_t n_samples) {
    ESP_LOGI(TAG, "session_save_note_audio: session_id=%" PRIu32 " n_samples=%zu sd_mounted=%d",
             session_id, n_samples, sd_is_mounted());

    if (!pcm || n_samples == 0 || session_id == 0) {
        ESP_LOGE(TAG, "session_save_note_audio: invalid args (pcm=%p n_samples=%zu session_id=%" PRIu32 ")",
                 (const void *)pcm, n_samples, session_id);
        return ESP_ERR_INVALID_ARG;
    }
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "session_save_note_audio: SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(session_id, &m);
    if (err != ESP_OK || m.id != session_id || m.deleted) {
        ESP_LOGE(TAG, "session_save_note_audio: meta_read failed or mismatched (err=%s, m.id=%" PRIu32 ", deleted=%d)",
                 esp_err_to_name(err), m.id, m.deleted);
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    ensure_session_audio_dir(session_id);
    char path[64];
    session_note_audio_path(session_id, path, sizeof(path));
    ESP_LOGI(TAG, "session_save_note_audio: writing to %s", path);
    err = write_wav_file(path, pcm, n_samples);

    if (err == ESP_OK) {
        m.has_note_audio = 1;
        err = meta_write(&m);
        ESP_LOGI(TAG, "session_save_note_audio: meta_write -> %s", esp_err_to_name(err));
        if (err == ESP_OK && session_id == s_active_session_id && s_cache_valid) {
            s_active_cache.has_note_audio = 1;
        }
    } else {
        ESP_LOGE(TAG, "session_save_note_audio: write_wav_file failed: %s", esp_err_to_name(err));
    }

    UNLOCK();
    return err;
}

esp_err_t session_delete_note_audio(uint32_t session_id) {
    if (session_id == 0) return ESP_ERR_INVALID_ARG;

    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(session_id, &m);
    if (err != ESP_OK || m.id != session_id || m.deleted) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }

    char path[64];
    session_note_audio_path(session_id, path, sizeof(path));
    remove(path);

    m.has_note_audio = 0;
    err = meta_write(&m);
    if (err == ESP_OK && session_id == s_active_session_id && s_cache_valid) {
        s_active_cache.has_note_audio = 0;
    }

    UNLOCK();
    return err;
}

esp_err_t session_save_tag_audio(uint16_t audio_seq, const int16_t *pcm, size_t n_samples) {
    ESP_LOGI(TAG, "session_save_tag_audio: audio_seq=%u n_samples=%zu sd_mounted=%d",
             (unsigned)audio_seq, n_samples, sd_is_mounted());

    if (!pcm || n_samples == 0) {
        ESP_LOGE(TAG, "session_save_tag_audio: invalid args (pcm=%p n_samples=%zu)", (const void *)pcm, n_samples);
        return ESP_ERR_INVALID_ARG;
    }
    if (!sd_is_mounted()) {
        ESP_LOGE(TAG, "session_save_tag_audio: SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    LOCK();
    if (!s_cache_valid || s_active_session_id == 0) {
        ESP_LOGE(TAG, "session_save_tag_audio: no active session (cache_valid=%d active_id=%" PRIu32 ")",
                 s_cache_valid, s_active_session_id);
        UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    ensure_session_audio_dir(s_active_session_id);
    char path[64];
    session_tag_audio_path(s_active_session_id, audio_seq, path, sizeof(path));
    ESP_LOGI(TAG, "session_save_tag_audio: writing to %s", path);
    esp_err_t err = write_wav_file(path, pcm, n_samples);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "session_save_tag_audio: write_wav_file failed: %s", esp_err_to_name(err));
    }

    UNLOCK();
    return err;
}

// ===== Tag records (active session only) =====

esp_err_t session_add_tag(const tag_record_t *tag) {
    if (!tag) return ESP_ERR_INVALID_ARG;

    LOCK();
    if (!s_cache_valid || s_active_session_id == 0) { UNLOCK(); return ESP_ERR_INVALID_STATE; }

    char path[32];
    make_sess_path(s_active_session_id, path, sizeof(path));

    FILE *f = fopen(path, "rb+");
    bool found = false;
    long match_offset = -1;

    if (f) {
        tag_record_t tmp;
        long offset = 0;
        while (fread(&tmp, sizeof(tag_record_t), 1, f) == 1) {
            if (strncmp(tmp.eid, tag->eid, sizeof(tmp.eid)) == 0) {
                match_offset = offset;
                found = true;
                break;
            }
            offset += sizeof(tag_record_t);
        }
    }

    esp_err_t err;
    if (found && match_offset >= 0) {
        fseek(f, match_offset, SEEK_SET);
        size_t n = fwrite(tag, sizeof(tag_record_t), 1, f);
        fclose(f);
        err = (n == 1) ? ESP_OK : ESP_FAIL;
        ESP_LOGI(TAG, "Tag updated in session %lu: %s", (unsigned long)s_active_session_id, tag->eid);
    } else {
        if (f) fclose(f);
        f = fopen(path, "ab");
        if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
        size_t n = fwrite(tag, sizeof(tag_record_t), 1, f);
        fclose(f);
        if (n == 1) {
            s_active_cache.tag_count++;
            s_active_cache.updated_at = time(NULL);
            err = meta_write(&s_active_cache);
            ESP_LOGI(TAG, "Tag added to session %lu: %s (count: %lu)",
                     (unsigned long)s_active_session_id, tag->eid, (unsigned long)s_active_cache.tag_count);
        } else {
            err = ESP_FAIL;
        }
    }

    UNLOCK();
    return err;
}

bool session_get_tag(const char *eid, tag_record_t *out) {
    if (!eid) return false;

    LOCK();
    if (!s_cache_valid || s_active_session_id == 0) { UNLOCK(); return false; }

    char path[32];
    make_sess_path(s_active_session_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) { UNLOCK(); return false; }

    tag_record_t tmp;
    bool found = false;
    while (fread(&tmp, sizeof(tag_record_t), 1, f) == 1) {
        if (strncmp(tmp.eid, eid, sizeof(tmp.eid)) == 0) {
            if (out) *out = tmp;
            found = true;
            break;
        }
    }
    fclose(f);

    UNLOCK();
    return found;
}

esp_err_t session_update_tag(const tag_record_t *tag) {
    if (!tag) return ESP_ERR_INVALID_ARG;

    LOCK();
    if (!s_cache_valid || s_active_session_id == 0) { UNLOCK(); return ESP_ERR_INVALID_STATE; }

    char path[32];
    make_sess_path(s_active_session_id, path, sizeof(path));

    FILE *f = fopen(path, "rb+");
    if (!f) { UNLOCK(); return ESP_ERR_NOT_FOUND; }

    tag_record_t tmp;
    long offset = 0;
    bool found = false;
    while (fread(&tmp, sizeof(tag_record_t), 1, f) == 1) {
        if (strncmp(tmp.eid, tag->eid, sizeof(tmp.eid)) == 0) {
            found = true;
            break;
        }
        offset += sizeof(tag_record_t);
    }

    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (found) {
        fseek(f, offset, SEEK_SET);
        size_t n = fwrite(tag, sizeof(tag_record_t), 1, f);
        err = (n == 1) ? ESP_OK : ESP_FAIL;
    }
    fclose(f);

    UNLOCK();
    return err;
}

uint32_t session_list_tags(tag_record_t *out, uint32_t max_out) {
    if (!out || max_out == 0) return 0;

    LOCK();
    if (!s_cache_valid || s_active_session_id == 0) { UNLOCK(); return 0; }

    char path[32];
    make_sess_path(s_active_session_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) { UNLOCK(); return 0; }

    uint32_t n = 0;
    while (n < max_out && fread(&out[n], sizeof(tag_record_t), 1, f) == 1) {
        n++;
    }
    fclose(f);

    UNLOCK();
    return n;
}

uint32_t session_get_tag_count(void) {
    LOCK();
    uint32_t n = s_cache_valid ? s_active_cache.tag_count : 0;
    UNLOCK();
    return n;
}

esp_err_t session_clear_tags(void) {
    LOCK();
    if (!s_cache_valid || s_active_session_id == 0) { UNLOCK(); return ESP_ERR_INVALID_STATE; }

    char path[32];
    make_sess_path(s_active_session_id, path, sizeof(path));
    remove(path);

    s_active_cache.tag_count = 0;
    s_active_cache.updated_at = time(NULL);
    esp_err_t err = meta_write(&s_active_cache);

    UNLOCK();
    return err;
}

// ===== Vaccine configuration =====

uint32_t vaccine_list(vaccine_cfg_t *out, uint32_t max_out) {
    if (!out || max_out == 0) return 0;

    LOCK();
    FILE *f = fopen(VACCINE_PATH, "rb");
    if (!f) { UNLOCK(); return 0; }

    uint32_t n = 0;
    vaccine_cfg_t v;
    while (n < max_out && fread(&v, sizeof(v), 1, f) == 1) {
        if (v.id != 0 && v.active) {
            out[n++] = v;
        }
    }
    fclose(f);
    UNLOCK();
    return n;
}

esp_err_t vaccine_add(const char *name, uint8_t *out_id) {
    if (!name || !name[0]) return ESP_ERR_INVALID_ARG;

    LOCK();
    uint8_t max_id = 0;
    FILE *f = fopen(VACCINE_PATH, "rb");
    if (f) {
        vaccine_cfg_t v;
        while (fread(&v, sizeof(v), 1, f) == 1) {
            if (v.id > max_id) max_id = v.id;
        }
        fclose(f);
    }

    if (max_id >= VACCINE_LIST_MAX) { UNLOCK(); return ESP_ERR_NO_MEM; }

    vaccine_cfg_t nv = {0};
    nv.id = max_id + 1;
    nv.active = 1;
    strncpy(nv.name, name, sizeof(nv.name) - 1);

    f = fopen(VACCINE_PATH, "ab");
    if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
    size_t n = fwrite(&nv, sizeof(nv), 1, f);
    fclose(f);

    esp_err_t err = ESP_FAIL;
    if (n == 1) {
        if (out_id) *out_id = nv.id;
        ESP_LOGI(TAG, "Added vaccine %d \"%s\"", nv.id, nv.name);
        err = ESP_OK;
    }
    UNLOCK();
    return err;
}

esp_err_t vaccine_delete(uint8_t id) {
    if (id == 0) return ESP_ERR_INVALID_ARG;

    LOCK();
    static vaccine_cfg_t buf[VACCINE_LIST_MAX];
    int total = 0;
    bool found = false;

    FILE *f = fopen(VACCINE_PATH, "rb");
    if (f) {
        vaccine_cfg_t v;
        while (total < VACCINE_LIST_MAX && fread(&v, sizeof(v), 1, f) == 1) {
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

bool vaccine_get_name(uint8_t id, char *out_name, size_t max_len) {
    if (id == 0 || !out_name) return false;

    LOCK();
    FILE *f = fopen(VACCINE_PATH, "rb");
    if (!f) { UNLOCK(); return false; }

    vaccine_cfg_t v;
    bool found = false;
    while (fread(&v, sizeof(v), 1, f) == 1) {
        if (v.id == id && v.active) {
            strncpy(out_name, v.name, max_len - 1);
            out_name[max_len - 1] = '\0';
            found = true;
            break;
        }
    }
    fclose(f);
    UNLOCK();
    return found;
}

// ===== Test configuration (mirrors vaccine API) =====

uint32_t test_list(test_cfg_t *out, uint32_t max_out) {
    if (!out || max_out == 0) return 0;

    LOCK();
    FILE *f = fopen(TEST_PATH, "rb");
    if (!f) { UNLOCK(); return 0; }

    uint32_t n = 0;
    test_cfg_t t;
    while (n < max_out && fread(&t, sizeof(t), 1, f) == 1) {
        if (t.id != 0 && t.active) {
            out[n++] = t;
        }
    }
    fclose(f);
    UNLOCK();
    return n;
}

esp_err_t test_add(const char *name, uint8_t *out_id) {
    if (!name || !name[0]) return ESP_ERR_INVALID_ARG;

    LOCK();
    uint8_t max_id = 0;
    FILE *f = fopen(TEST_PATH, "rb");
    if (f) {
        test_cfg_t t;
        while (fread(&t, sizeof(t), 1, f) == 1) {
            if (t.id > max_id) max_id = t.id;
        }
        fclose(f);
    }

    if (max_id >= TEST_LIST_MAX) { UNLOCK(); return ESP_ERR_NO_MEM; }

    test_cfg_t nt = {0};
    nt.id = max_id + 1;
    nt.active = 1;
    strncpy(nt.name, name, sizeof(nt.name) - 1);

    f = fopen(TEST_PATH, "ab");
    if (!f) { UNLOCK(); return ESP_ERR_NO_MEM; }
    size_t n = fwrite(&nt, sizeof(nt), 1, f);
    fclose(f);

    esp_err_t err = ESP_FAIL;
    if (n == 1) {
        if (out_id) *out_id = nt.id;
        ESP_LOGI(TAG, "Added test %d \"%s\"", nt.id, nt.name);
        err = ESP_OK;
    }
    UNLOCK();
    return err;
}

esp_err_t test_delete(uint8_t id) {
    if (id == 0) return ESP_ERR_INVALID_ARG;

    LOCK();
    static test_cfg_t buf[TEST_LIST_MAX];
    int total = 0;
    bool found = false;

    FILE *f = fopen(TEST_PATH, "rb");
    if (f) {
        test_cfg_t t;
        while (total < TEST_LIST_MAX && fread(&t, sizeof(t), 1, f) == 1) {
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

bool test_get_name(uint8_t id, char *out_name, size_t max_len) {
    if (id == 0 || !out_name) return false;

    LOCK();
    FILE *f = fopen(TEST_PATH, "rb");
    if (!f) { UNLOCK(); return false; }

    test_cfg_t t;
    bool found = false;
    while (fread(&t, sizeof(t), 1, f) == 1) {
        if (t.id == id && t.active) {
            strncpy(out_name, t.name, max_len - 1);
            out_name[max_len - 1] = '\0';
            found = true;
            break;
        }
    }
    fclose(f);
    UNLOCK();
    return found;
}

// ===== BLE sync support =====

esp_err_t session_mark_synced(uint32_t session_id) {
    LOCK();
    session_meta_t m;
    esp_err_t err = meta_read(session_id, &m);
    if (err != ESP_OK || m.id != session_id || m.deleted) {
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }
    m.synced = 1;
    err = meta_write(&m);
    if (err == ESP_OK && session_id == s_active_session_id && s_cache_valid) {
        s_active_cache.synced = 1;
    }
    UNLOCK();
    return err;
}

uint32_t session_list_records_paged(uint32_t session_id, tag_record_t *out,
                                     uint32_t max_out, uint32_t offset) {
    if (!out || max_out == 0 || session_id == 0) return 0;

    LOCK();
    char path[32];
    make_sess_path(session_id, path, sizeof(path));

    FILE *f = fopen(path, "rb");
    if (!f) { UNLOCK(); return 0; }

    if (offset > 0) {
        fseek(f, (long)(offset * sizeof(tag_record_t)), SEEK_SET);
    }

    uint32_t n = 0;
    while (n < max_out && fread(&out[n], sizeof(tag_record_t), 1, f) == 1) {
        n++;
    }
    fclose(f);

    UNLOCK();
    return n;
}
