#include "vibrator.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "vibrator";
static bool s_enabled = true;

#ifdef VIBRATOR_ENABLED

// Each queue item is one pulse segment: motor on for on_ms, then silent for gap_ms.
typedef struct {
    uint16_t on_ms;
    uint16_t gap_ms;
} pulse_t;

static QueueHandle_t s_queue = NULL;

static void vibrator_task(void *arg)
{
    pulse_t p;
    while (true) {
        xQueueReceive(s_queue, &p, portMAX_DELAY);
        gpio_set_level((gpio_num_t)VIBRATOR_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(p.on_ms));
        gpio_set_level((gpio_num_t)VIBRATOR_PIN, 0);
        if (p.gap_ms) {
            vTaskDelay(pdMS_TO_TICKS(p.gap_ms));
        }
    }
}

static void enqueue(uint16_t on_ms, uint16_t gap_ms)
{
    if (!s_enabled || !s_queue) return;
    pulse_t p = { on_ms, gap_ms };
    xQueueSend(s_queue, &p, 0);   // non-blocking; drop if queue full
}

#endif // VIBRATOR_ENABLED

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

    s_queue = xQueueCreate(8, sizeof(pulse_t));
    xTaskCreate(vibrator_task, "vibrator", 1024, NULL, 3, NULL);
    ESP_LOGI(TAG, "Vibrator ready on GPIO %d", VIBRATOR_PIN);
#else
    ESP_LOGI(TAG, "Vibrator disabled (VIBRATOR_ENABLED not set)");
#endif
}

void vibrator_success(void)
{
#ifdef VIBRATOR_ENABLED
    // One long pulse — matches the green flash on new scan
    enqueue(400, 0);
#endif
}

void vibrator_duplicate(void)
{
#ifdef VIBRATOR_ENABLED
    // Two short pulses — matches the red flash on duplicate
    enqueue(120, 120);
    enqueue(120, 0);
#endif
}

void vibrator_set_enabled(bool enabled)
{
    s_enabled = enabled;
}
