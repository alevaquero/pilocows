#include "soft_rtc.h"
#include "bsp_rtc.h"
#include "nvs_storage.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <sys/time.h>

static const char *TAG = "soft_rtc";
static const char *NVS_NAMESPACE = "pilocows";
static const char *NVS_KEY_LAST_TIME = "last_time";

bool soft_rtc_init(void) {
    if (rtc_init()) {
        ESP_LOGI(TAG, "Time restored from hardware RTC (DS3231)");
        return true;
    }
    ESP_LOGW(TAG, "No hardware RTC found - falling back to NVS-saved time");

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

    if (!rtc_set_time(t)) {
        ESP_LOGW(TAG, "Hardware RTC write failed/unavailable - time only set in software+NVS");
    }

    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u64(handle, NVS_KEY_LAST_TIME, (uint64_t)t);
        nvs_commit(handle);
        nvs_close(handle);
    }

    ESP_LOGI(TAG, "Time set to %lld", (long long)t);
}

// Cached in RAM after first read — soft_rtc_get_local_tm() is called from
// two independent 1-second LVGL timers (screen_session_menu.c and
// screen_scan.c each run their own clock_tick_cb, and LVGL timers keep
// ticking regardless of which screen is actually visible), so hitting NVS
// on every single call here would mean two blocking NVS flash opens+reads
// per second, forever — real regression, not just log noise. Invalidated
// (well, just overwritten) the moment the user actually changes it.
static int16_t s_tz_offset_min = 0;
static bool s_tz_offset_loaded = false;

int16_t soft_rtc_get_tz_offset_min(void) {
    if (!s_tz_offset_loaded) {
        AppSettings s = {0};
        nvs_load_settings(&s);
        s_tz_offset_min = s.tz_offset_min;
        s_tz_offset_loaded = true;
    }
    return s_tz_offset_min;
}

void soft_rtc_set_tz_offset_min(int16_t minutes) {
    AppSettings s = {0};
    nvs_load_settings(&s);
    s.tz_offset_min = minutes;
    nvs_save_settings(&s);
    s_tz_offset_min = minutes;
    s_tz_offset_loaded = true;
    ESP_LOGI(TAG, "Timezone offset set to %d min", (int)minutes);
}

void soft_rtc_get_local_tm(struct tm *out) {
    time_t local = time(NULL) + (time_t)soft_rtc_get_tz_offset_min() * 60;
    // gmtime_r, not localtime_r: the system TZ is always UTC (never set
    // otherwise anywhere in this firmware), so gmtime_r does a plain
    // epoch->calendar conversion with no further (and here, double) offset
    // applied on top of the one already added above.
    gmtime_r(&local, out);
}

void soft_rtc_set_local_tm(const struct tm *local_tm) {
    struct tm tmp = *local_tm;
    // mktime() interprets tmp's fields as if they were UTC (system TZ is
    // always UTC) — giving the UTC instant that shares the same numeric
    // wall-clock digits the user entered. Subtracting the offset converts
    // that to the true UTC instant of the LOCAL moment the user meant.
    time_t naive_utc = mktime(&tmp);
    time_t true_utc = naive_utc - (time_t)soft_rtc_get_tz_offset_min() * 60;
    soft_rtc_set_time(true_utc);
}
