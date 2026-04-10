#include "vibrator.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "vibrator";
static bool s_enabled = true;

void vibrator_init(void)
{
#ifdef VIBRATOR_ENABLED
    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(VIBRATOR_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)VIBRATOR_PIN, 0);
    ESP_LOGI(TAG, "Vibrator ready on GPIO %d", VIBRATOR_PIN);
#else
    ESP_LOGI(TAG, "Vibrator disabled (VIBRATOR_ENABLED not set)");
#endif
}

void vibrator_pulse(void)
{
#ifdef VIBRATOR_ENABLED
    if (!s_enabled) return;
    gpio_set_level((gpio_num_t)VIBRATOR_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level((gpio_num_t)VIBRATOR_PIN, 0);
#endif
}

void vibrator_set_enabled(bool enabled)
{
    s_enabled = enabled;
}
