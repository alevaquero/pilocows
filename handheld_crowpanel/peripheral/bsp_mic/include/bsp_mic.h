#ifndef _BSP_MIC_H_
#define _BSP_MIC_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "driver/i2s_pdm.h"

// PDM microphone pins (CrowPanel Advance P4, onboard mic).
#define MIC_GPIO_CLK 24
#define MIC_GPIO_DIN 25

#define MIC_SAMPLE_RATE_HZ 16000

// Hardware PDM->PCM digital gain range and factory default (see
// mic_set_gain()/bsp_mic.c for why this is a hardware, not software, knob).
#define MIC_GAIN_MIN 1
#define MIC_GAIN_MAX 15
#define MIC_GAIN_DEFAULT 4

// Initialize the I2S PDM RX channel (16kHz/16-bit mono). Call once at
// startup (main.c). Safe to call again — later calls are no-ops that just
// return the result of the first call, so independent screens can each
// check readiness without coordinating who calls this first.
esp_err_t mic_init(void);

// True once mic_init() has succeeded.
bool mic_is_ready(void);

// Blocking read of up to max_samples mono int16 samples. Returns the actual
// count read in *samples_read (may be less than max_samples on timeout).
esp_err_t mic_read(int16_t *buf, size_t max_samples, size_t *samples_read, TickType_t timeout);

// Adjusts the hardware PDM->PCM digital gain (MIC_GAIN_MIN..MIC_GAIN_MAX)
// at runtime, clamping out-of-range values. This changes the analog/digital
// pickup level at the mic itself, unlike a software post-multiply gain,
// which can only amplify a signal that's already clipped — so this is the
// only way to actually fix clipping/distortion at the source. Briefly
// disables and re-enables the I2S channel to apply.
esp_err_t mic_set_gain(uint8_t amplify_num);

// Current hardware gain (MIC_GAIN_DEFAULT until mic_set_gain() is called).
uint8_t mic_get_gain(void);

#endif
