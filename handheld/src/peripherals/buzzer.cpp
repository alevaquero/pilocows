#include "buzzer.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "buzzer";
static bool s_enabled = true;

// ---------------------------------------------------------------------------
// LEDC config — uses TIMER_1 / CHANNEL_1 (TIMER_0/CHANNEL_0 = backlight)
// Buzzer is passive, driven by a PNP 9012 transistor (active LOW):
//   GPIO LOW  → transistor ON  → buzzer sounds
//   GPIO HIGH → transistor OFF → silent
// Optimal range: 2–5 kHz. Use ledc_stop(idle=1) for clean silence.
// ---------------------------------------------------------------------------
#define BUZ_SPEED    LEDC_LOW_SPEED_MODE
#define BUZ_TIMER    LEDC_TIMER_1
#define BUZ_CHANNEL  LEDC_CHANNEL_1
#define BUZ_RES      LEDC_TIMER_10_BIT
#define BUZ_DUTY_50  (512u)   // 50% of 10-bit = square wave

// Tone frequencies — within 2–5 kHz optimal range
#define FREQ_SUCCESS   3000u  // warm, low-ish beep for confirmed scan
#define FREQ_DUPLICATE 4500u  // higher, more alert-like for duplicate

#ifdef BUZZER_ENABLED

typedef struct {
    uint32_t freq_hz;
    uint16_t on_ms;
    uint16_t gap_ms;
} beep_t;

static QueueHandle_t s_queue = NULL;

static void tone_on(uint32_t freq_hz)
{
    ledc_timer_config_t timer = {
        .speed_mode      = BUZ_SPEED,
        .duty_resolution = BUZ_RES,
        .timer_num       = BUZ_TIMER,
        .freq_hz         = freq_hz,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);
    ledc_set_duty(BUZ_SPEED, BUZ_CHANNEL, BUZ_DUTY_50);
    ledc_update_duty(BUZ_SPEED, BUZ_CHANNEL);
}

static void tone_off(void)
{
    // idle_level=1 → GPIO stays HIGH → PNP transistor OFF → silent
    ledc_stop(BUZ_SPEED, BUZ_CHANNEL, 1);
}

static void buzzer_task(void *arg)
{
    beep_t b;
    while (true) {
        xQueueReceive(s_queue, &b, portMAX_DELAY);
        tone_on(b.freq_hz);
        vTaskDelay(pdMS_TO_TICKS(b.on_ms));
        tone_off();
        if (b.gap_ms) {
            vTaskDelay(pdMS_TO_TICKS(b.gap_ms));
        }
    }
}

static void enqueue(uint32_t freq_hz, uint16_t on_ms, uint16_t gap_ms)
{
    if (!s_enabled || !s_queue) return;
    beep_t b = { freq_hz, on_ms, gap_ms };
    xQueueSend(s_queue, &b, 0);
}

#endif // BUZZER_ENABLED

void buzzer_init(void)
{
#ifdef BUZZER_ENABLED
    // Initial timer config (frequency set per-beep; this just initialises HW)
    ledc_timer_config_t timer = {
        .speed_mode      = BUZ_SPEED,
        .duty_resolution = BUZ_RES,
        .timer_num       = BUZ_TIMER,
        .freq_hz         = FREQ_SUCCESS,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num   = BUZZER_PIN,
        .speed_mode = BUZ_SPEED,
        .channel    = BUZ_CHANNEL,
        .timer_sel  = BUZ_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel);
    tone_off();   // ensure silent at startup

    s_queue = xQueueCreate(8, sizeof(beep_t));
    xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "Buzzer ready on GPIO %d — LEDC TIMER1/CH1, 2-5kHz PWM", BUZZER_PIN);
#else
    ESP_LOGI(TAG, "Buzzer disabled (BUZZER_ENABLED not set)");
#endif
}

void buzzer_success(void)
{
#ifdef BUZZER_ENABLED
    // 3 kHz, 400 ms — warm tone matching the green flash
    enqueue(FREQ_SUCCESS, 400, 0);
#endif
}

void buzzer_duplicate(void)
{
#ifdef BUZZER_ENABLED
    // 4.5 kHz, two short beeps — higher pitch matching the red flash
    enqueue(FREQ_DUPLICATE, 120, 120);
    enqueue(FREQ_DUPLICATE, 120, 0);
#endif
}

void buzzer_set_enabled(bool enabled)
{
    s_enabled = enabled;
}
