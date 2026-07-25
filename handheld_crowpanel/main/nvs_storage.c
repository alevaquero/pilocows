#include "nvs_storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>
#include <time.h>

static const char *TAG = "nvs_storage";
static const char *NVS_NAMESPACE = "pilocows";
static const char *NVS_KEY_SETTINGS = "settings";
static const char *NVS_KEY_SCANS = "scans";

esp_err_t nvs_load_settings(AppSettings *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, using defaults");
        strcpy(settings->language, "en");
        settings->buzzer_enabled = true;
        settings->vibrator_enabled = true;
        return ESP_OK;
    }

    size_t required_size = sizeof(AppSettings);
    err = nvs_get_blob(handle, NVS_KEY_SETTINGS, settings, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Settings not found in NVS, using defaults");
        strcpy(settings->language, "en");
        settings->buzzer_enabled = true;
        settings->vibrator_enabled = true;
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading settings: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Settings loaded from NVS");
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_save_settings(const AppSettings *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_SETTINGS, (void *)settings, sizeof(AppSettings));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write settings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Settings saved to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to commit settings: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_load_scans(ScanList *scan_list) {
    if (!scan_list) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found");
        scan_list->count = 0;
        return ESP_OK;
    }

    size_t required_size = sizeof(ScanList);
    err = nvs_get_blob(handle, NVS_KEY_SCANS, scan_list, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Scans not found in NVS");
        scan_list->count = 0;
        err = ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error reading scans: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Loaded %d scans from NVS", scan_list->count);
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_save_scans(const ScanList *scan_list) {
    if (!scan_list) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY_SCANS, (void *)scan_list, sizeof(ScanList));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write scans: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    err = nvs_commit(handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Scans saved to NVS (%d records)", scan_list->count);
    } else {
        ESP_LOGE(TAG, "Failed to commit scans: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

esp_err_t nvs_add_scan(const char *eid) {
    if (!eid) return ESP_ERR_INVALID_ARG;

    ScanList scan_list;
    esp_err_t err = nvs_load_scans(&scan_list);
    if (err != ESP_OK) return err;

    if (scan_list.count >= 100) {
        ESP_LOGW(TAG, "Scan list full (max 100)");
        return ESP_ERR_NO_MEM;
    }

    strncpy(scan_list.scans[scan_list.count].eid, eid, sizeof(scan_list.scans[0].eid) - 1);
    scan_list.scans[scan_list.count].timestamp = (uint32_t)time(NULL);
    scan_list.count++;

    ESP_LOGI(TAG, "Added scan: %s (total: %d)", eid, scan_list.count);
    return nvs_save_scans(&scan_list);
}

esp_err_t nvs_clear_scans(void) {
    ScanList scan_list = {0};
    ESP_LOGI(TAG, "Clearing all scans");
    return nvs_save_scans(&scan_list);
}
