#ifndef _FEEDBACK_DRIVER_H_
#define _FEEDBACK_DRIVER_H_

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// Initialize the vibrator driver (PWM-based) and the speaker (scan sound
// effects). No buzzer — this board has real speakers, so the scan
// confirmation sounds play through those instead of PWM-tone hardware.
esp_err_t feedback_init(void);

// Deinitialize feedback drivers
esp_err_t feedback_deinit(void);

// Vibrator control (intensity 0-100%, duration in ms)
esp_err_t vibrator_pulse(uint32_t intensity_percent, uint32_t duration_ms);

// Continuous vibrator control (intensity 0-100%, 0 to stop)
esp_err_t vibrator_set(uint32_t intensity_percent);

// Enable/disable vibrator globally (from Settings screen / NVS)
void feedback_set_vibrator_enabled(bool enabled);

// Speaker volume (0-100%) for the scan sound effects, from Settings screen / NVS.
void feedback_set_speaker_volume(uint8_t percent);

// Named feedback patterns — used by the scan flow:
//   sound_new_tag()    / vibrator_success()   — new tag scanned.
//   sound_duplicate()  / vibrator_duplicate() — tag already in the session.
// The sound half is a no-op if the speaker failed to init; volume 0 also
// silences it (see feedback_set_speaker_volume).
void sound_new_tag(void);
void sound_duplicate(void);
void vibrator_success(void);
void vibrator_duplicate(void);

// Play the scan sound effects on demand (Settings screen "test" buttons) —
// same sounds as sound_new_tag()/sound_duplicate(), just always played
// regardless of context, so the user can preview them; still respects
// speaker volume, including 0 (silent).
void audio_test_new_tag(void);
void audio_test_duplicate(void);

// Run the scan vibration patterns on demand (Settings screen "test"
// buttons) — same patterns as vibrator_success()/vibrator_duplicate(), but
// always run regardless of the vibrator's enabled/disabled switch, so the
// user can feel the motor and pattern even while testing with it off.
void vibrator_test_new_tag(void);
void vibrator_test_duplicate(void);

#endif
