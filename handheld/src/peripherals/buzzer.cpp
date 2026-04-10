#include "buzzer.h"
#include "board_config.h"
#include "esp_log.h"

static const char *TAG = "buzzer";
static bool s_enabled = true;

void buzzer_init(void)
{
#ifdef AUDIO_ENABLED
    // TODO: Initialize I2S driver for audio amplifier
    // Pins: LRCK=AUDIO_PIN_LRCK, BCLK=AUDIO_PIN_BCLK, DOUT=AUDIO_PIN_DOUT
    ESP_LOGI(TAG, "Buzzer init (I2S) — TODO: implement when speaker is wired");
#else
    ESP_LOGI(TAG, "Buzzer disabled (AUDIO_ENABLED not set)");
#endif
}

void buzzer_beep(void)
{
#ifdef AUDIO_ENABLED
    if (!s_enabled) return;
    // TODO: Play a short 1kHz tone via I2S for ~100ms
#endif
}

void buzzer_error(void)
{
#ifdef AUDIO_ENABLED
    if (!s_enabled) return;
    // TODO: Play a descending two-tone error pattern
#endif
}

void buzzer_set_enabled(bool enabled)
{
    s_enabled = enabled;
}
