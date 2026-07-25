#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "lvgl.h"

static const char *TAG = "main";
static int tap_count = 0;
static lv_obj_t *counter_label = NULL;

static void on_tap(lv_event_t *e) {
    tap_count++;
    char buf[32];
    snprintf(buf, sizeof(buf), "Tap counter: %d", tap_count);
    lv_label_set_text(counter_label, buf);
    ESP_LOGI(TAG, "Tapped! Count: %d", tap_count);
}

void app_main(void)
{
    ESP_LOGI(TAG, "========== Pilocows Handheld CrowPanel ==========");
    ESP_LOGI(TAG, "Firmware version: 0.1.0-alpha");

    // Initialize LDO power rails (critical for CrowPanel!)
    ESP_LOGI(TAG, "Initializing power management...");
    esp_ldo_channel_handle_t ldo4 = NULL;
    esp_ldo_channel_config_t ldo4_config = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo4_config, &ldo4);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LDO4: %s", esp_err_to_name(err));
    }

    // Initialize GPIO ISR service
    err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
    }

    // Initialize I2C (needed for touch)
    ESP_LOGI(TAG, "Initializing I2C...");
    err = i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Initialize touch BEFORE display (critical for touch to work!)
    ESP_LOGI(TAG, "Initializing touch...");
    err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch: %s", esp_err_to_name(err));
    }

    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "========== System Ready ==========");

    // Create a simple counter demo using LVGL
    counter_label = lv_label_create(lv_scr_act());
    lv_label_set_text(counter_label, "Tap counter: 0");
    lv_obj_center(counter_label);
    lv_obj_set_size(counter_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_flag(counter_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(counter_label, on_tap, LV_EVENT_CLICKED, NULL);

    ESP_LOGI(TAG, "Demo UI ready - tap the counter to increment");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "running...");
    }
}
