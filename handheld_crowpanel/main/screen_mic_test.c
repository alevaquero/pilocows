#include "screen_mic_test.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "i18n.h"
#include "strings_en.h"
#include "bsp_mic.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "scr_mic_test";

#define MIC_MAX_RECORD_SECONDS 10
#define MIC_MAX_SAMPLES (MIC_MAX_RECORD_SECONDS * MIC_SAMPLE_RATE_HZ)
#define MIC_READ_CHUNK_SAMPLES 512

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_chart_series = NULL;
static lv_obj_t *s_btn_record = NULL;
static lv_obj_t *s_btn_play = NULL;
static lv_timer_t *s_ui_timer = NULL;

static bool s_mic_ready = false;
static int16_t *s_rec_buf = NULL; // PSRAM, MIC_MAX_SAMPLES mono int16 samples

static volatile bool s_recording = false;
static volatile bool s_stop_requested = false;
static volatile size_t s_rec_samples = 0; // samples captured so far / in the last recording
static volatile int s_level_pct = 0;      // 0-100, most recent chunk's peak level
static TaskHandle_t s_rec_task = NULL;
static bool s_was_recording = false; // edge-detect recording -> idle in the UI timer

static void update_status_label(void) {
    if (s_recording) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s %.1fs", i18n_t(STR_MIC_RECORDING), (float)s_rec_samples / MIC_SAMPLE_RATE_HZ);
        lv_label_set_text(s_lbl_status, buf);
    } else if (s_rec_samples > 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s: %.1fs", i18n_t(STR_MIC_RECORDED), (float)s_rec_samples / MIC_SAMPLE_RATE_HZ);
        lv_label_set_text(s_lbl_status, buf);
    } else {
        lv_label_set_text(s_lbl_status, i18n_t(STR_MIC_HOLD_TO_RECORD));
    }
}

// ── Recording (own FreeRTOS task — never touches LVGL) ──────────────────────

// Boost quiet (e.g. far-away) recordings up to a consistent target level
// after capture. Only ever amplifies — a recording that's already loud
// enough is left untouched, so this can't make a close-up recording worse.
static void normalize_recording(size_t n_samples) {
    if (n_samples == 0) return;

    int16_t peak = 0;
    for (size_t i = 0; i < n_samples; i++) {
        int16_t av = s_rec_buf[i] < 0 ? (int16_t)(-s_rec_buf[i]) : s_rec_buf[i];
        if (av > peak) peak = av;
    }
    if (peak == 0) return; // pure silence - nothing to normalize

    const int16_t target_peak = (int16_t)(0.85f * 32767);
    float scale = (float)target_peak / (float)peak;
    if (scale <= 1.0f) return; // already loud enough
    if (scale > 15.0f) scale = 15.0f; // cap so near-silent noise doesn't turn into a blast

    for (size_t i = 0; i < n_samples; i++) {
        int32_t v = (int32_t)(s_rec_buf[i] * scale);
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        s_rec_buf[i] = (int16_t)v;
    }
}

static void record_task(void *arg) {
    (void)arg;
    size_t total = 0;
    int16_t chunk[MIC_READ_CHUNK_SAMPLES];

    while (!s_stop_requested && total < MIC_MAX_SAMPLES) {
        size_t want = MIC_READ_CHUNK_SAMPLES;
        if (total + want > MIC_MAX_SAMPLES) want = MIC_MAX_SAMPLES - total;

        size_t got = 0;
        esp_err_t err = mic_read(chunk, want, &got, pdMS_TO_TICKS(200));
        if (err != ESP_OK || got == 0) continue;

        int16_t peak = 0;
        for (size_t i = 0; i < got; i++) {
            int16_t av = chunk[i] < 0 ? (int16_t)(-chunk[i]) : chunk[i];
            if (av > peak) peak = av;
        }
        s_level_pct = (peak * 100) / 32767;

        memcpy(&s_rec_buf[total], chunk, got * sizeof(int16_t));
        total += got;
        s_rec_samples = total;
    }

    normalize_recording(total);

    s_recording = false;
    s_rec_task = NULL;
    vTaskDelete(NULL);
}

static void start_recording(void) {
    if (!s_mic_ready || s_recording) return;
    s_stop_requested = false;
    s_rec_samples = 0;
    s_level_pct = 0;
    s_recording = true;
    lv_chart_set_all_value(s_chart, s_chart_series, 0);
    lv_obj_add_state(s_btn_play, LV_STATE_DISABLED);
    xTaskCreate(record_task, "mic_rec", 4096, NULL, 4, &s_rec_task);
}

static void stop_recording(void) {
    s_stop_requested = true;
}

// ── Playback (reuses bsp_audio; duplicates mono -> stereo on the fly) ───────

static void play_task(void *arg) {
    (void)arg;
    size_t n_samples = s_rec_samples;
    if (n_samples == 0) {
        vTaskDelete(NULL);
        return;
    }

    int16_t *stereo = malloc(n_samples * 2 * sizeof(int16_t));
    if (!stereo) {
        ESP_LOGW(TAG, "No memory for playback buffer");
        vTaskDelete(NULL);
        return;
    }
    for (size_t i = 0; i < n_samples; i++) {
        // The recording is already normalized to near-peak level (see
        // normalize_recording()), so only a small extra margin is needed here.
        int32_t v = (int32_t)(s_rec_buf[i] * 1.2f);
        if (v > 32767) v = 32767;
        else if (v < -32768) v = -32768;
        stereo[2 * i] = (int16_t)v;
        stereo[2 * i + 1] = (int16_t)v;
    }

    set_audio_ctrl(true);
    vTaskDelay(pdMS_TO_TICKS(60)); // let the amp finish its wake-up ramp before real audio
    size_t written = 0;
    i2s_channel_write(get_audio_handle(), stereo, n_samples * 2 * sizeof(int16_t), &written, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(80));
    set_audio_ctrl(false);

    free(stereo);
    vTaskDelete(NULL);
}

// ── Events ───────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e) {
    (void)e;
    stop_recording();
    ui_manager_show(SCREEN_SETTINGS);
}

static void on_record_pressed(lv_event_t *e) { (void)e; start_recording(); }
static void on_record_released(lv_event_t *e) { (void)e; stop_recording(); }

static void on_play(lv_event_t *e) {
    (void)e;
    if (s_recording || s_rec_samples == 0) return;
    xTaskCreate(play_task, "mic_play", 3072, NULL, 3, NULL);
}

static void ui_timer_cb(lv_timer_t *t) {
    (void)t;
    if (s_recording) {
        lv_chart_set_next_value(s_chart, s_chart_series, s_level_pct);
        update_status_label();
        s_was_recording = true;
    } else if (s_was_recording) {
        // Recording just finished (edge from recording -> idle).
        s_was_recording = false;
        lv_chart_set_all_value(s_chart, s_chart_series, 0);
        update_status_label();
        if (s_rec_samples > 0) lv_obj_clear_state(s_btn_play, LV_STATE_DISABLED);
    }
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    lv_chart_set_all_value(s_chart, s_chart_series, 0);
    update_status_label();
    lv_obj_add_state(s_btn_play, LV_STATE_DISABLED);
    if (s_rec_samples > 0) lv_obj_clear_state(s_btn_play, LV_STATE_DISABLED);
}

// ── Screen creation ──────────────────────────────────────────────────────────

void screen_mic_test_create(void) {
    s_rec_buf = heap_caps_malloc(MIC_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_rec_buf) {
        ESP_LOGE(TAG, "Failed to allocate %d-byte recording buffer", (int)(MIC_MAX_SAMPLES * sizeof(int16_t)));
    }

    s_mic_ready = mic_is_ready() && (s_rec_buf != NULL);
    if (!s_mic_ready) {
        ESP_LOGW(TAG, "Mic not ready (see main_task init log)");
    }

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_text_font(s_scr, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header (80px tall, standard dark navy w/ back "<") ───────────────────
    lv_obj_t *hdr = lv_obj_create(s_scr);
    lv_obj_set_size(hdr, 480, 80);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x2c3e50), 0);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 70, 70);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 6);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_back, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(lbl_back);

    s_lbl_title = lv_label_create(hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_MIC_TEST_TITLE));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Status line ───────────────────────────────────────────────────────────
    s_lbl_status = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_status, i18n_t(STR_MIC_HOLD_TO_RECORD));
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_app_24, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_width(s_lbl_status, 440);
    lv_obj_set_pos(s_lbl_status, 20, 104);

    // ── Level meter (scrolling bar chart, moves while recording) ────────────
    s_chart = lv_chart_create(s_scr);
    lv_obj_set_size(s_chart, 420, 150);
    lv_obj_set_pos(s_chart, 30, 150);
    lv_obj_set_style_bg_color(s_chart, lv_color_hex(0xF3F1EC), LV_PART_MAIN);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_point_count(s_chart, 26);
    lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(s_chart, 3, 0);
    s_chart_series = lv_chart_add_series(s_chart, lv_palette_main(LV_PALETTE_RED), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(s_chart, s_chart_series, 0);

    // ── Big record button (press-and-hold) ───────────────────────────────────
    s_btn_record = lv_btn_create(s_scr);
    lv_obj_set_size(s_btn_record, 220, 220);
    lv_obj_set_pos(s_btn_record, 130, 340);
    lv_obj_set_style_radius(s_btn_record, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_record, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_record, lv_color_hex(0x922B21), LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_record, 6, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_btn_record, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(s_btn_record, on_record_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_btn_record, on_record_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_btn_record, on_record_released, LV_EVENT_PRESS_LOST, NULL);
    if (!s_mic_ready) {
        lv_obj_add_state(s_btn_record, LV_STATE_DISABLED);
        lv_label_set_text(s_lbl_status, i18n_t(STR_MIC_UNAVAILABLE));
    }

    // ── Play button ───────────────────────────────────────────────────────────
    s_btn_play = lv_btn_create(s_scr);
    lv_obj_set_size(s_btn_play, 90, 90);
    lv_obj_set_pos(s_btn_play, 195, 590);
    lv_obj_set_style_radius(s_btn_play, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_play, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
    lv_obj_add_state(s_btn_play, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_btn_play, on_play, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_play = lv_label_create(s_btn_play);
    lv_label_set_text(lbl_play, LV_SYMBOL_PLAY);
    lv_obj_set_style_text_font(lbl_play, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_play, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(lbl_play);

    s_ui_timer = lv_timer_create(ui_timer_cb, 100, NULL);

    ESP_LOGI(TAG, "Mic test screen created (mic_ready=%d)", s_mic_ready);
}

void screen_mic_test_load(void) {
    lv_scr_load(s_scr);
}

void screen_mic_test_refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(STR_MIC_TEST_TITLE));
    update_status_label();
}
