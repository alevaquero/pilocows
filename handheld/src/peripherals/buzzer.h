#pragma once

// Buzzer via onboard I2S audio amplifier.
// TODO: Wire speaker to SPK+/SPK- connector, then uncomment AUDIO_ENABLED
//       in board_config.h and implement these functions.

void buzzer_init(void);
void buzzer_beep(void);    // Short confirmation beep
void buzzer_error(void);   // Error tone pattern
void buzzer_set_enabled(bool enabled);
