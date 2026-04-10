#pragma once

#include <stdbool.h>

// Vibrator motor on GPIO 21 (EXT_IO6).
// TODO: Wire motor driver transistor to EXT_IO6, then uncomment
//       VIBRATOR_ENABLED in board_config.h and implement these functions.

void vibrator_init(void);
void vibrator_pulse(void);      // Short vibration pulse (~200ms)
void vibrator_set_enabled(bool enabled);
