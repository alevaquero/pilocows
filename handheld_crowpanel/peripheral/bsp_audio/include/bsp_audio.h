#ifndef _BSP_AUDIO_H_
#define _BSP_AUDIO_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2s_std.h"

// I2S pins for the onboard speaker amplifier (CrowPanel Advance P4).
// The amplifier's shutdown/enable line is not a direct ESP32-P4 GPIO — it's
// driven through the STC8H1KXX management MCU (STC8_GPIO_OUT_AUDIO_SD), same
// chip that controls the LCD backlight.
#define AUDIO_GPIO_LRCLK 21
#define AUDIO_GPIO_BCLK  22
#define AUDIO_GPIO_SDATA 23

#define AUDIO_SAMPLE_RATE_HZ 16000

// Initialize the I2S TX channel (16kHz/16-bit/stereo, matches the WAV assets
// embedded in main/sounds/). Call once at startup, after stc8_i2c_init().
esp_err_t audio_init(void);

// Enable/disable the speaker amplifier via the STC8 management MCU.
esp_err_t set_audio_ctrl(bool enable);

// Handle to the I2S TX channel, for direct i2s_channel_write() calls.
i2s_chan_handle_t get_audio_handle(void);

#endif
