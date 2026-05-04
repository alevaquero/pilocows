#include "buttons.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "buttons";

#define LONG_PRESS_MS  1000

static const gpio_num_t BTN_PINS[BTN_COUNT] = {
    (gpio_num_t)BTN_PIN_SCAN,
};

static button_callback_t s_callback = NULL;

typedef struct {
    bool     last_state;      // true = pressed (active low → logic inverted)
    int64_t  press_time_us;
    bool     long_press_fired;
} btn_state_t;

static btn_state_t s_state[BTN_COUNT] = {};

static void buttons_task(void *arg)
{
    while (true) {
        int64_t now = esp_timer_get_time();

        for (int i = 0; i < BTN_COUNT; i++) {
            // Active low: pin reads 0 when pressed
            bool pressed = (gpio_get_level(BTN_PINS[i]) == 0);

            if (pressed && !s_state[i].last_state) {
                // Falling edge — button pressed
                s_state[i].press_time_us   = now;
                s_state[i].long_press_fired = false;
                if (s_callback) s_callback((button_id_t)i, BTN_EVENT_PRESS);

            } else if (!pressed && s_state[i].last_state) {
                // Rising edge — button released
                if (s_callback) s_callback((button_id_t)i, BTN_EVENT_RELEASE);

            } else if (pressed && !s_state[i].long_press_fired) {
                // Still held — check for long press
                int64_t held_ms = (now - s_state[i].press_time_us) / 1000;
                if (held_ms >= LONG_PRESS_MS) {
                    s_state[i].long_press_fired = true;
                    if (s_callback) s_callback((button_id_t)i, BTN_EVENT_LONG_PRESS);
                }
            }

            s_state[i].last_state = pressed;
        }

        vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));
    }
}

void buttons_init(button_callback_t on_event)
{
    s_callback = on_event;

    for (int i = 0; i < BTN_COUNT; i++) {
        gpio_config_t cfg = {
            .pin_bit_mask = BIT64(BTN_PINS[i]),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&cfg);
    }

    xTaskCreate(buttons_task, "buttons", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "Buttons ready (SCAN=%d)", BTN_PIN_SCAN);
}

void buttons_poll(void)
{
    // No-op: polling is handled in the buttons_task
}
