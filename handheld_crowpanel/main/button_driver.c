#include "button_driver.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "button";

#define BUTTON_UP_GPIO    48
#define BUTTON_DOWN_GPIO  31
#define BUTTON_SELECT_GPIO 32
#define DEBOUNCE_MS 20

static const int button_gpios[BUTTON_COUNT] = {
    BUTTON_UP_GPIO,
    BUTTON_DOWN_GPIO,
    BUTTON_SELECT_GPIO
};

static const char *button_names[BUTTON_COUNT] = {
    "UP", "DOWN", "SELECT"
};

static button_callback_t s_callback = NULL;
static uint32_t s_last_press_time[BUTTON_COUNT] = {0};

static void IRAM_ATTR button_isr_handler(void *arg) {
    button_id_t button = (button_id_t)(uintptr_t)arg;
    uint32_t now = esp_log_timestamp();

    if ((now - s_last_press_time[button]) < DEBOUNCE_MS) {
        return;
    }

    s_last_press_time[button] = now;

    int level = gpio_get_level(button_gpios[button]);
    if (s_callback) {
        s_callback(button, level == 0 ? 1 : 0);
    }
}

esp_err_t button_init(button_callback_t callback) {
    if (!callback) return ESP_ERR_INVALID_ARG;

    s_callback = callback;
    ESP_LOGI(TAG, "Initializing button driver (GPIO 48/31/32)");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 0,
        .pull_down_en = 0,
        .pull_up_en = 1,
    };

    for (int i = 0; i < BUTTON_COUNT; i++) {
        io_conf.pin_bit_mask |= (1ULL << button_gpios[i]);
    }

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO: %s", esp_err_to_name(err));
        return err;
    }

    for (int i = 0; i < BUTTON_COUNT; i++) {
        err = gpio_isr_handler_add(button_gpios[i], button_isr_handler, (void *)(uintptr_t)i);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ISR handler for button %s: %s",
                     button_names[i], esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "Button %s configured on GPIO %d", button_names[i], button_gpios[i]);
    }

    return ESP_OK;
}

esp_err_t button_deinit(void) {
    for (int i = 0; i < BUTTON_COUNT; i++) {
        gpio_isr_handler_remove(button_gpios[i]);
    }
    s_callback = NULL;
    ESP_LOGI(TAG, "Button driver deinitialized");
    return ESP_OK;
}

int button_get_state(button_id_t button) {
    if (button >= BUTTON_COUNT) return -1;
    return gpio_get_level(button_gpios[button]) == 0 ? 1 : 0;
}
