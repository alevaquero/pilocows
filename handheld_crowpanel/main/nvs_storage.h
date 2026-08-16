#ifndef _NVS_STORAGE_H_
#define _NVS_STORAGE_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define DEFAULT_SPEAKER_VOLUME 80

typedef struct {
    char language[3];
    bool buzzer_enabled;    // vestigial — this board has no buzzer (speaker
                             // handles scan sounds instead). Field kept, not
                             // read anywhere, so old saved blobs don't shift
                             // the byte offsets of speaker_volume/mic_gain
                             // below; never reuse this slot for something else.
    bool vibrator_enabled;
    uint8_t speaker_volume; // 0-100; added after buzzer/vibrator, keep last —
                             // nvs_load_settings() pre-fills a default before
                             // reading, so old (smaller) saved blobs migrate
                             // cleanly instead of leaving this uninitialized.
    uint8_t mic_gain;        // MIC_GAIN_MIN-MIC_GAIN_MAX (bsp_mic.h); same
                              // append-only-at-the-end migration rule as
                              // speaker_volume above.
    int16_t tz_offset_min;   // Minutes east of UTC (e.g. -300 for UTC-05:00).
                              // Stored in minutes for header-room to support
                              // finer steps later, but the Date & Time
                              // screen's UI currently only sets whole-hour
                              // values (see TZ_STEP_MIN, screen_datetime.c).
                              // The system clock
                              // (soft_rtc.c) always stores true UTC — this is
                              // purely a display/entry conversion, applied by
                              // soft_rtc_get_local_tm()/soft_rtc_set_local_tm().
                              // Same append-only-at-the-end rule as above;
                              // defaults to 0 (UTC) on old saved blobs.
} AppSettings;

// Settings
esp_err_t nvs_load_settings(AppSettings *settings);
esp_err_t nvs_save_settings(const AppSettings *settings);

#endif
