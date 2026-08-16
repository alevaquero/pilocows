#include "feedback_driver.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>

static const char *TAG = "feedback";

// Embedded scan-feedback sound effects (main/sounds/*.wav, 16kHz/16-bit/
// stereo PCM — see main/CMakeLists.txt EMBED_FILES). Symbol names are
// derived from the embedded file's path relative to main/.
extern const uint8_t sounds_new_tag_wav_start[] asm("_binary_new_tag_wav_start");
extern const uint8_t sounds_new_tag_wav_end[]   asm("_binary_new_tag_wav_end");
extern const uint8_t sounds_duplicate_wav_start[] asm("_binary_duplicate_wav_start");
extern const uint8_t sounds_duplicate_wav_end[]   asm("_binary_duplicate_wav_end");

typedef struct { const uint8_t *pcm; size_t len; } wav_clip_t;

static wav_clip_t s_clip_new_tag;
static wav_clip_t s_clip_duplicate;
static bool s_audio_ready = false;
static uint8_t s_speaker_volume = 80; // overwritten from NVS via feedback_set_speaker_volume()

#define VIBRATOR_GPIO  30
#define VIBRATOR_LEDC_CHANNEL  LEDC_CHANNEL_1
#define LEDC_SPEED_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_RESOLUTION  LEDC_TIMER_10_BIT
#define LEDC_FREQUENCY_BASE    5000

static TaskHandle_t vibrator_timer_task = NULL;
static TaskHandle_t vibrator_pattern_task = NULL;
static bool s_vibrator_enabled = true;

static void vibrator_timer_task_func(void *arg) {
    uint32_t duration = (uintptr_t)arg;
    vTaskDelay(pdMS_TO_TICKS(duration));
    ledc_set_duty(LEDC_SPEED_MODE, VIBRATOR_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_SPEED_MODE, VIBRATOR_LEDC_CHANNEL);
    vibrator_timer_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t feedback_init(void) {
    ESP_LOGI(TAG, "Initializing feedback drivers (vibrator GPIO 30)");

    // Configure LEDC timer
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_RESOLUTION,
        .freq_hz = LEDC_FREQUENCY_BASE,
        .speed_mode = LEDC_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK,
    };

    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(err));
        return err;
    }

    // Configure vibrator LEDC channel
    ledc_channel_config_t vibrator_channel = {
        .channel = VIBRATOR_LEDC_CHANNEL,
        .duty = 0,
        .gpio_num = VIBRATOR_GPIO,
        .speed_mode = LEDC_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER_0,
        .intr_type = LEDC_INTR_DISABLE,
    };

    err = ledc_channel_config(&vibrator_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure vibrator channel: %s", esp_err_to_name(err));
        return err;
    }

    // Speaker (scan sound effects) — non-fatal if it fails to init; the
    // named patterns below just stay silent in that case (no PWM buzzer
    // fallback on this board — see sound_new_tag()/sound_duplicate()).
    set_audio_ctrl(false);
    err = audio_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Speaker init failed, scan sounds will be silent: %s",
                 esp_err_to_name(err));
    } else {
        s_clip_new_tag.pcm = sounds_new_tag_wav_start + 44; // skip the 44-byte WAV header
        s_clip_new_tag.len = (size_t)(sounds_new_tag_wav_end - sounds_new_tag_wav_start) - 44;
        s_clip_duplicate.pcm = sounds_duplicate_wav_start + 44;
        s_clip_duplicate.len = (size_t)(sounds_duplicate_wav_end - sounds_duplicate_wav_start) - 44;
        s_audio_ready = true;
    }

    ESP_LOGI(TAG, "Feedback drivers initialized");
    return ESP_OK;
}

static void play_wav_task(void *arg) {
    wav_clip_t *clip = (wav_clip_t *)arg;
    uint8_t vol = s_speaker_volume;
    if (vol == 0) {
        vTaskDelete(NULL);
        return;
    }

    // Software volume: the clip lives in flash (const, can't scale in place),
    // so copy to a scratch buffer and scale each 16-bit sample before writing.
    int16_t *buf = malloc(clip->len);
    size_t written = 0;
    if (buf) {
        const int16_t *src = (const int16_t *)clip->pcm;
        size_t n_samples = clip->len / sizeof(int16_t);
        for (size_t i = 0; i < n_samples; i++) {
            buf[i] = (int16_t)(((int32_t)src[i] * vol) / 100);
        }
        set_audio_ctrl(true);
        // The amp needs a short wake-up window after its shutdown pin is
        // released before it actually passes audio (a "pop guard" ramp-up
        // common to class-D amps) - without this gap the start of the clip
        // plays into a still-muting amp and gets swallowed. The I2S channel
        // stays enabled with auto_clear on, so this is genuine silence on
        // the line, not a stall.
        vTaskDelay(pdMS_TO_TICKS(60));
        i2s_channel_write(get_audio_handle(), buf, clip->len, &written, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(80)); // let the DMA buffers fully drain before cutting the amp
        set_audio_ctrl(false);
        free(buf);
    } else {
        ESP_LOGW(TAG, "No memory for volume-scaled playback buffer (%d bytes)", (int)clip->len);
    }
    vTaskDelete(NULL);
}

esp_err_t feedback_deinit(void) {
    if (vibrator_timer_task) {
        vTaskDelete(vibrator_timer_task);
        vibrator_timer_task = NULL;
    }
    ledc_set_duty(LEDC_SPEED_MODE, VIBRATOR_LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_SPEED_MODE, VIBRATOR_LEDC_CHANNEL);
    ESP_LOGI(TAG, "Feedback drivers deinitialized");
    return ESP_OK;
}

esp_err_t vibrator_pulse(uint32_t intensity_percent, uint32_t duration_ms) {
    if (intensity_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = vibrator_set(intensity_percent);
    if (err != ESP_OK) return err;

    if (vibrator_timer_task) {
        vTaskDelete(vibrator_timer_task);
    }

    xTaskCreate(vibrator_timer_task_func, "vibrator_timer", 1024, (void *)(uintptr_t)duration_ms, 1, &vibrator_timer_task);

    return ESP_OK;
}

esp_err_t vibrator_set(uint32_t intensity_percent) {
    if (intensity_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    uint32_t max_duty = (1 << LEDC_TIMER_RESOLUTION) - 1;
    uint32_t duty = (max_duty * intensity_percent) / 100;

    ledc_set_duty(LEDC_SPEED_MODE, VIBRATOR_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_SPEED_MODE, VIBRATOR_LEDC_CHANNEL);

    return ESP_OK;
}

void feedback_set_vibrator_enabled(bool enabled) {
    s_vibrator_enabled = enabled;
}

void feedback_set_speaker_volume(uint8_t percent) {
    s_speaker_volume = percent > 100 ? 100 : percent;
}

// ---- Named patterns ----

static void vibrator_duplicate_task(void *arg) {
    (void)arg;
    vibrator_set(100);
    vTaskDelay(pdMS_TO_TICKS(120));
    vibrator_set(0);
    vTaskDelay(pdMS_TO_TICKS(120));
    vibrator_set(100);
    vTaskDelay(pdMS_TO_TICKS(120));
    vibrator_set(0);
    vibrator_pattern_task = NULL;
    vTaskDelete(NULL);
}

void sound_new_tag(void) {
    if (!s_audio_ready) return;
    // Bright ascending two-note confirm chime, played through the speaker.
    xTaskCreate(play_wav_task, "snd_new_tag", 3072, &s_clip_new_tag, 3, NULL);
}

void sound_duplicate(void) {
    if (!s_audio_ready) return;
    // Neutral flat double-blip, played through the speaker.
    xTaskCreate(play_wav_task, "snd_dup", 3072, &s_clip_duplicate, 3, NULL);
}

// Runs the pattern unconditionally — shared by the named (enabled-gated)
// functions below and by the Settings screen's always-on test buttons.
static void run_new_tag_pattern(void) {
    // One long pulse — matches a confirmed new scan.
    vibrator_pulse(100, 400);
}

static void run_duplicate_pattern(void) {
    if (vibrator_pattern_task) vTaskDelete(vibrator_pattern_task);
    xTaskCreate(vibrator_duplicate_task, "vibrator_dup", 1024, NULL, 3, &vibrator_pattern_task);
}

void vibrator_success(void) {
    if (!s_vibrator_enabled) return;
    run_new_tag_pattern();
}

void vibrator_duplicate(void) {
    if (!s_vibrator_enabled) return;
    run_duplicate_pattern();
}

void audio_test_new_tag(void) {
    if (!s_audio_ready) return;
    xTaskCreate(play_wav_task, "snd_test_new", 3072, &s_clip_new_tag, 3, NULL);
}

void audio_test_duplicate(void) {
    if (!s_audio_ready) return;
    xTaskCreate(play_wav_task, "snd_test_dup", 3072, &s_clip_duplicate, 3, NULL);
}

void vibrator_test_new_tag(void) {
    run_new_tag_pattern();
}

void vibrator_test_duplicate(void) {
    run_duplicate_pattern();
}
