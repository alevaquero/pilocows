#include "scan_storage.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_err.h"
#include <stdio.h>
#include <string.h>

static const char *TAG        = "storage";
static const char *SCAN_FILE  = "/spiffs/scans.jsonl";  // JSON Lines format
static bool        s_mounted  = false;

void scan_storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = NULL,  // Use first spiffs partition
        .max_files              = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return;
    }
    s_mounted = true;

    size_t total, used;
    esp_spiffs_info(NULL, &total, &used);
    ESP_LOGI(TAG, "SPIFFS mounted: %d/%d bytes used", (int)used, (int)total);
}

bool scan_storage_save(const scan_record_t *record)
{
    if (!s_mounted) return false;

    FILE *f = fopen(SCAN_FILE, "a");
    if (!f) {
        ESP_LOGE(TAG, "Cannot open scan file for append");
        return false;
    }

    // Append one JSON line per record (JSON Lines / NDJSON format)
    int written = fprintf(f,
        "{\"eid\":\"%s\",\"scanned_at\":\"%s\","
        "\"event_type\":\"%s\",\"notes\":\"%s\"}\n",
        record->eid, record->scanned_at,
        record->event_type, record->notes
    );
    fclose(f);

    if (written < 0) {
        ESP_LOGE(TAG, "Write error");
        return false;
    }

    ESP_LOGI(TAG, "Saved scan: %s [%s]", record->eid, record->event_type);
    return true;
}

uint32_t scan_storage_count(void)
{
    if (!s_mounted) return 0;

    FILE *f = fopen(SCAN_FILE, "r");
    if (!f) return 0;

    uint32_t count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '{') count++;
    }
    fclose(f);
    return count;
}

int scan_storage_to_json(char *buf, size_t buf_len)
{
    if (!s_mounted) {
        snprintf(buf, buf_len, "[]");
        return 2;
    }

    FILE *f = fopen(SCAN_FILE, "r");
    if (!f) {
        snprintf(buf, buf_len, "[]");
        return 2;
    }

    size_t pos = 0;
    buf[pos++] = '[';

    char line[256];
    bool first = true;
    while (fgets(line, sizeof(line), f)) {
        size_t line_len = strlen(line);
        // Strip trailing newline
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[--line_len] = '\0';
        }
        if (line[0] != '{') continue;

        size_t needed = (first ? 0 : 1) + line_len;
        if (pos + needed + 2 >= buf_len) {  // +2 for ']' and null
            ESP_LOGW(TAG, "JSON buffer too small — truncating");
            break;
        }
        if (!first) buf[pos++] = ',';
        memcpy(&buf[pos], line, line_len);
        pos += line_len;
        first = false;
    }
    fclose(f);

    buf[pos++] = ']';
    buf[pos]   = '\0';
    return (int)pos;
}

void scan_storage_clear(void)
{
    if (!s_mounted) return;
    remove(SCAN_FILE);
    ESP_LOGI(TAG, "Scan list cleared");
}
