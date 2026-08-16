#include "audio_codec_util.h"
#include "esp_g711_enc.h"
#include "esp_g711_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "audio_codec";

// Espressif's published stack requirements for this library are ~40KB for
// the encoder and ~20KB for the decoder — far more than the small dedicated
// tasks this file's callers already use (e.g. screen_audio_note.c's
// play_file_task has a 3.5KB stack). Calling these directly on a caller's
// task silently corrupts its stack instead of erroring cleanly (observed:
// a non-rebooting watchdog hang with garbage-looking backtraces, right
// after a successful-looking encode failure). Every public entry point
// here instead runs the real work on a short-lived helper task sized for
// the library's own documented needs, so callers can keep calling these
// like ordinary blocking functions regardless of their own stack size.
#define ENCODE_TASK_STACK_BYTES (48 * 1024)
#define DECODE_TASK_STACK_BYTES (24 * 1024)

static esp_err_t encode_alaw_impl(const int16_t *pcm16k, size_t n_samples_16k,
                                   uint8_t **out_alaw, size_t *out_len) {
    if (!pcm16k || n_samples_16k < 2 || !out_alaw || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t n_samples_8k = n_samples_16k / 2;

    esp_g711_enc_config_t cfg = ESP_G711_ENC_CONFIG_DEFAULT();
    cfg.sample_rate = ESP_AUDIO_SAMPLE_RATE_8K;
    cfg.channel = ESP_AUDIO_MONO;
    cfg.bits_per_sample = ESP_AUDIO_BIT16;

    void *enc_hd = NULL;
    if (esp_g711a_enc_open(&cfg, sizeof(cfg), &enc_hd) != ESP_AUDIO_ERR_OK || !enc_hd) {
        ESP_LOGE(TAG, "esp_g711a_enc_open failed");
        return ESP_FAIL;
    }

    // The encoder requires the input length to be an exact multiple of its
    // internal frame size (20ms of audio -> 320 bytes/160 samples at 8kHz),
    // not just a multiple of one sample — confirmed the hard way (real
    // recordings essentially never land on an exact frame boundary). Pad
    // the tail with silence up to the next whole frame, encode that, then
    // keep only the bytes corresponding to real input samples (G711 is a
    // strict 1:1 sample:byte mapping, so the padding's encoded tail is
    // simply discarded rather than needing to be re-derived).
    int in_frame_bytes = 0, out_frame_bytes = 0;
    if (esp_g711_enc_get_frame_size(enc_hd, &in_frame_bytes, &out_frame_bytes) != ESP_AUDIO_ERR_OK
        || in_frame_bytes <= 0 || out_frame_bytes <= 0) {
        ESP_LOGE(TAG, "esp_g711_enc_get_frame_size failed");
        esp_g711_enc_close(enc_hd);
        return ESP_FAIL;
    }
    size_t in_frame_samples = (size_t)in_frame_bytes / sizeof(int16_t);
    size_t n_frames = (n_samples_8k + in_frame_samples - 1) / in_frame_samples;
    size_t n_samples_padded = n_frames * in_frame_samples;

    int16_t *pcm8k = calloc(n_samples_padded, sizeof(int16_t)); // zero-padded tail
    if (!pcm8k) {
        esp_g711_enc_close(enc_hd);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < n_samples_8k; i++) {
        int32_t a = pcm16k[2 * i];
        int32_t b = pcm16k[2 * i + 1];
        pcm8k[i] = (int16_t)((a + b) / 2);
    }

    size_t out_buf_len = n_frames * (size_t)out_frame_bytes;
    uint8_t *alaw_buf = malloc(out_buf_len);
    if (!alaw_buf) {
        free(pcm8k);
        esp_g711_enc_close(enc_hd);
        return ESP_ERR_NO_MEM;
    }

    esp_audio_enc_in_frame_t in_frame = {
        .buffer = (uint8_t *)pcm8k,
        .len = (uint32_t)(n_samples_padded * sizeof(int16_t)),
    };
    esp_audio_enc_out_frame_t out_frame = {
        .buffer = alaw_buf,
        .len = (uint32_t)out_buf_len,
    };
    esp_audio_err_t aerr = esp_g711_enc_process(enc_hd, &in_frame, &out_frame);
    esp_g711_enc_close(enc_hd);
    free(pcm8k);

    if (aerr != ESP_AUDIO_ERR_OK || out_frame.encoded_bytes < n_samples_8k) {
        ESP_LOGE(TAG, "esp_g711_enc_process failed: %d (encoded_bytes=%" PRIu32 ")",
                 (int)aerr, out_frame.encoded_bytes);
        free(alaw_buf);
        return ESP_FAIL;
    }

    *out_alaw = alaw_buf;
    *out_len = n_samples_8k; // trim the padding-derived tail
    return ESP_OK;
}

static esp_err_t decode_alaw_to_16k_impl(const uint8_t *alaw, size_t alaw_len,
                                          int16_t **out_pcm, size_t *out_n_samples) {
    if (!alaw || alaw_len == 0 || !out_pcm || !out_n_samples) {
        return ESP_ERR_INVALID_ARG;
    }

    void *dec_hd = NULL;
    if (esp_g711_dec_open(NULL, 0, &dec_hd) != ESP_AUDIO_ERR_OK || !dec_hd) {
        ESP_LOGE(TAG, "esp_g711_dec_open failed");
        return ESP_FAIL;
    }

    // Unlike the encoder, the decoder has no fixed-frame-multiple
    // requirement — it's designed to be called in a loop consuming
    // `raw.consumed` bytes at a time, which handles any input length.
    int16_t *pcm8k = malloc(alaw_len * sizeof(int16_t));
    if (!pcm8k) {
        esp_g711_dec_close(dec_hd);
        return ESP_ERR_NO_MEM;
    }

    esp_audio_dec_in_raw_t raw = {
        .buffer = (uint8_t *)alaw,
        .len = (uint32_t)alaw_len,
    };
    size_t total_decoded_bytes = 0;
    esp_audio_err_t aerr = ESP_AUDIO_ERR_OK;
    // dec_info is an output-only param, but esp_g711a_dec_decode validates
    // it's non-NULL regardless (confirmed on hardware: passing NULL fails
    // every call with "Invalid parameter 'g711 information'", -5) — we
    // don't otherwise need the info it reports (sample rate etc. are
    // already known from how this file was encoded).
    esp_audio_dec_info_t dec_info;
    while (raw.len > 0) {
        esp_audio_dec_out_frame_t frame = {
            .buffer = (uint8_t *)pcm8k + total_decoded_bytes,
            .len = (uint32_t)(alaw_len * sizeof(int16_t) - total_decoded_bytes),
        };
        aerr = esp_g711a_dec_decode(dec_hd, &raw, &frame, &dec_info);
        if (aerr != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "esp_g711a_dec_decode failed: %d", (int)aerr);
            break;
        }
        total_decoded_bytes += frame.decoded_size;
        raw.buffer += raw.consumed;
        raw.len -= raw.consumed;
        if (raw.consumed == 0) break; // safety net against a stuck loop
    }
    esp_g711_dec_close(dec_hd);

    if (aerr != ESP_AUDIO_ERR_OK || total_decoded_bytes == 0) {
        free(pcm8k);
        return ESP_FAIL;
    }

    size_t n_samples_8k = total_decoded_bytes / sizeof(int16_t);

    // Upsample 8kHz -> 16kHz via linear interpolation (see header comment
    // for why this only happens here, not in the stored/transferred file).
    size_t n_samples_16k = n_samples_8k * 2;
    int16_t *pcm16k = malloc(n_samples_16k * sizeof(int16_t));
    if (!pcm16k) {
        free(pcm8k);
        return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < n_samples_8k; i++) {
        int16_t cur = pcm8k[i];
        int16_t next = (i + 1 < n_samples_8k) ? pcm8k[i + 1] : cur;
        pcm16k[2 * i] = cur;
        pcm16k[2 * i + 1] = (int16_t)(((int32_t)cur + next) / 2);
    }
    free(pcm8k);

    *out_pcm = pcm16k;
    *out_n_samples = n_samples_16k;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Dedicated-task wrappers (see stack-size comment above)
// ---------------------------------------------------------------------------

typedef struct {
    const int16_t *pcm16k;
    size_t n_samples_16k;
    uint8_t **out_alaw;
    size_t *out_len;
    esp_err_t result;
    SemaphoreHandle_t done;
} encode_task_args_t;

static void encode_task(void *arg) {
    encode_task_args_t *a = (encode_task_args_t *)arg;
    a->result = encode_alaw_impl(a->pcm16k, a->n_samples_16k, a->out_alaw, a->out_len);
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

esp_err_t audio_codec_encode_alaw(const int16_t *pcm16k, size_t n_samples_16k,
                                   uint8_t **out_alaw, size_t *out_len) {
    encode_task_args_t args = {
        .pcm16k = pcm16k,
        .n_samples_16k = n_samples_16k,
        .out_alaw = out_alaw,
        .out_len = out_len,
        .result = ESP_FAIL,
        .done = xSemaphoreCreateBinary(),
    };
    if (!args.done) return ESP_ERR_NO_MEM;

    if (xTaskCreate(encode_task, "alaw_enc", ENCODE_TASK_STACK_BYTES, &args, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create alaw_enc task (stack=%d)", ENCODE_TASK_STACK_BYTES);
        vSemaphoreDelete(args.done);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(args.done, portMAX_DELAY);
    vSemaphoreDelete(args.done);
    return args.result;
}

typedef struct {
    const uint8_t *alaw;
    size_t alaw_len;
    int16_t **out_pcm;
    size_t *out_n_samples;
    esp_err_t result;
    SemaphoreHandle_t done;
} decode_task_args_t;

static void decode_task(void *arg) {
    decode_task_args_t *a = (decode_task_args_t *)arg;
    a->result = decode_alaw_to_16k_impl(a->alaw, a->alaw_len, a->out_pcm, a->out_n_samples);
    xSemaphoreGive(a->done);
    vTaskDelete(NULL);
}

esp_err_t audio_codec_decode_alaw_to_16k(const uint8_t *alaw, size_t alaw_len,
                                          int16_t **out_pcm, size_t *out_n_samples) {
    decode_task_args_t args = {
        .alaw = alaw,
        .alaw_len = alaw_len,
        .out_pcm = out_pcm,
        .out_n_samples = out_n_samples,
        .result = ESP_FAIL,
        .done = xSemaphoreCreateBinary(),
    };
    if (!args.done) return ESP_ERR_NO_MEM;

    if (xTaskCreate(decode_task, "alaw_dec", DECODE_TASK_STACK_BYTES, &args, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create alaw_dec task (stack=%d)", DECODE_TASK_STACK_BYTES);
        vSemaphoreDelete(args.done);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(args.done, portMAX_DELAY);
    vSemaphoreDelete(args.done);
    return args.result;
}
