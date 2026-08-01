#ifndef _NVS_STORAGE_H_
#define _NVS_STORAGE_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#define DEFAULT_SPEAKER_VOLUME 80

typedef struct {
    char language[3];
    bool buzzer_enabled;
    bool vibrator_enabled;
    uint8_t speaker_volume; // 0-100; added after buzzer/vibrator, keep last —
                             // nvs_load_settings() pre-fills a default before
                             // reading, so old (smaller) saved blobs migrate
                             // cleanly instead of leaving this uninitialized.
} AppSettings;

// Settings
esp_err_t nvs_load_settings(AppSettings *settings);
esp_err_t nvs_save_settings(const AppSettings *settings);

#endif
