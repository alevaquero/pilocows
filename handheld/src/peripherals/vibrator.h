#pragma once

// Vibrator motor on GPIO 21 (EXT_IO6). Enabled via VIBRATOR_ENABLED in board_config.h.

void vibrator_init(void);

// Long single pulse (~400ms) — call on successful new scan (green flash).
void vibrator_success(void);

// Two short pulses (~120ms on, 120ms gap, 120ms on) — call on duplicate (red flash).
void vibrator_duplicate(void);

void vibrator_set_enabled(bool enabled);
