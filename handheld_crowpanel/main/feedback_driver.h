#ifndef _FEEDBACK_DRIVER_H_
#define _FEEDBACK_DRIVER_H_

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Initialize buzzer and vibrator drivers (PWM-based)
esp_err_t feedback_init(void);

// Deinitialize feedback drivers
esp_err_t feedback_deinit(void);

// Buzzer control (PWM frequency in Hz, duration in ms)
esp_err_t buzzer_beep(uint32_t frequency_hz, uint32_t duration_ms);

// Continuous buzzer control (PWM frequency in Hz, 0 to stop)
esp_err_t buzzer_set(uint32_t frequency_hz);

// Vibrator control (intensity 0-100%, duration in ms)
esp_err_t vibrator_pulse(uint32_t intensity_percent, uint32_t duration_ms);

// Continuous vibrator control (intensity 0-100%, 0 to stop)
esp_err_t vibrator_set(uint32_t intensity_percent);

// Enable/disable buzzer and vibrator globally (from Settings screen / NVS)
void feedback_set_buzzer_enabled(bool enabled);
void feedback_set_vibrator_enabled(bool enabled);

// Speaker volume (0-100%) for the scan sound effects, from Settings screen / NVS.
void feedback_set_speaker_volume(uint8_t percent);

// Named feedback patterns — used by the scan flow (mirrors original handheld):
//   *_success()   — one long beep/pulse (~400ms) for a new tag scanned.
//   *_duplicate() — two short beeps/pulses for a tag already in the session.
void buzzer_success(void);
void buzzer_duplicate(void);
void vibrator_success(void);
void vibrator_duplicate(void);

// Play the scan sound effects on demand (Settings screen "test" buttons) —
// ignores the buzzer-enabled toggle so the user can preview them even while
// disabled; still respects speaker volume, including 0 (silent).
void audio_test_new_tag(void);
void audio_test_duplicate(void);

#endif
