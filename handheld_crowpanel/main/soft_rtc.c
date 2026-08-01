#include "soft_rtc.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <sys/time.h>

static const char *TAG = "soft_rtc";
static const char *NVS_NAMESPACE = "pilocows";
static const char *NVS_KEY_LAST_TIME = "last_time";

bool soft_rtc_init(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved time in NVS - clock starts at epoch");
        return false;
    }

    uint64_t saved = 0;
    err = nvs_get_u64(handle, NVS_KEY_LAST_TIME, &saved);
    nvs_close(handle);

    if (err != ESP_OK || saved == 0) {
        ESP_LOGW(TAG, "No saved time in NVS - clock starts at epoch");
        return false;
    }

    struct timeval tv = { .tv_sec = (time_t)saved, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "Restored approximate time from NVS (last set: %llu)", (unsigned long long)saved);
    return true;
}

void soft_rtc_set_time(time_t t) {
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u64(handle, NVS_KEY_LAST_TIME, (uint64_t)t);
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Time set to %lld", (long long)t);
}
