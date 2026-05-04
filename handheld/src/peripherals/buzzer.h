#pragma once

// Buzzer — simple GPIO low-level-trigger on BUZZER_PIN (GPIO 11, EXT_IO2).
// Enabled via BUZZER_ENABLED in board_config.h.

void buzzer_init(void);

// Long single beep (~400ms) — call on successful new scan (green flash).
void buzzer_success(void);

// Two short beeps (~120ms on, 120ms gap, 120ms on) — call on duplicate (red flash).
void buzzer_duplicate(void);

void buzzer_set_enabled(bool enabled);
