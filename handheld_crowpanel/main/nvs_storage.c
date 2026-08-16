#include "nvs_storage.h"
#include "nvs_flash.h"
#include "bsp_mic.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_storage";
static const char *NVS_NAMESPACE = "pilocows";
static const char *NVS_KEY_SETTINGS = "settings";

esp_err_t nvs_load_settings(AppSettings *settings) {
    if (!settings) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found, using defaults");
        strcpy(settings->language, "en");
        settings->buzzer_enabled = true;
        settings->vibrator_enabled = true;
        settings->speaker_volume = DEFAULT_SPEAKER_VOLUME;
        settings->mic_gain = MIC_GAIN_DEFAULT;
        settings->tz_offset_min = 0;
        return ESP_OK;
    }

    // Pre-fill defaults before reading: a blob saved before speaker_volume/
    // mic_gain/tz_offset_min existed is smaller than sizeof(AppSettings), so
    // nvs_get_blob only overwrites the bytes it actually has, leaving these
    // untouched.
    settings->speaker_volume = DEFAULT_SPEAKER_VOLUME;
    settings->mic_gain = MIC_GAIN_DEFAULT;
    settings->tz_offset_min = 0;
    size_t required_size = sizeof(AppSettings);
    err = nvs_get_blob(handle, NVS_KEY_SETTINGS, settings, &required_size);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "Settings not found in NVS, using defaults");
        strcpy(settings->language, "en");
        settings->buzzer_enabled = true;
        settings->vibrator_enabled = true;
        settings->speaker_volume = DEFAULT_SPEAKER_VOLUME;
        settings->mic_gain = MIC_GAIN_DEFAULT;
        settings->tz_offset_min = 0;
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
