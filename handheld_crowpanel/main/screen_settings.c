#include "screen_settings.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "i18n.h"
#include "strings_en.h"
#include "feedback_driver.h"
#include "nvs_storage.h"
#include "app_version.h"
#include "bsp_display.h"
#include "lvgl.h"

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_back = NULL;
static lv_obj_t *s_lbl_language = NULL;
static lv_obj_t *s_lbl_buzzer = NULL;
static lv_obj_t *s_lbl_vibrator = NULL;
static lv_obj_t *s_lbl_brightness = NULL;
static lv_obj_t *s_lbl_volume = NULL;
static lv_obj_t *s_lbl_test_sounds = NULL;
static lv_obj_t *s_lbl_mic_test = NULL;
static lv_obj_t *s_lbl_mic_test_btn = NULL;
static lv_obj_t *s_lbl_datetime = NULL;
static lv_obj_t *s_lbl_datetime_btn = NULL;
static lv_obj_t *s_lbl_wifi = NULL;
static lv_obj_t *s_lbl_wifi_btn = NULL;
static lv_obj_t *s_lbl_vaccines = NULL;
static lv_obj_t *s_lbl_vaccines_btn = NULL;
static lv_obj_t *s_lbl_tests = NULL;
static lv_obj_t *s_lbl_tests_btn = NULL;
static lv_obj_t *s_lbl_sync = NULL;
static lv_obj_t *s_lbl_sync_btn = NULL;

static void refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_TITLE));
    lv_label_set_text(s_lbl_language, i18n_t(STR_SETTINGS_LANGUAGE));
    lv_label_set_text(s_lbl_buzzer, i18n_t(STR_SETTINGS_BUZZER));
    lv_label_set_text(s_lbl_vibrator, i18n_t(STR_SETTINGS_VIBRATOR));
    lv_label_set_text(s_lbl_brightness, i18n_t(STR_SETTINGS_BRIGHTNESS));
    lv_label_set_text(s_lbl_volume, i18n_t(STR_SETTINGS_VOLUME));
    lv_label_set_text(s_lbl_test_sounds, i18n_t(STR_SETTINGS_TEST_SOUNDS));
    lv_label_set_text(s_lbl_mic_test, i18n_t(STR_SETTINGS_MIC_TEST));
    lv_label_set_text(s_lbl_mic_test_btn, i18n_t(STR_WIFI_CONFIGURE));
    lv_label_set_text(s_lbl_datetime, i18n_t(STR_SETTINGS_DATETIME));
    lv_label_set_text(s_lbl_datetime_btn, i18n_t(STR_SETTINGS_SET_TIME));
    lv_label_set_text(s_lbl_wifi, i18n_t(STR_SETTINGS_WIFI));
    lv_label_set_text(s_lbl_wifi_btn, i18n_t(STR_WIFI_CONFIGURE));
    lv_label_set_text(s_lbl_vaccines, i18n_t(STR_SETTINGS_VACCINES));
    lv_label_set_text(s_lbl_vaccines_btn, i18n_t(STR_WIFI_CONFIGURE));
    lv_label_set_text(s_lbl_tests, i18n_t(STR_SETTINGS_TESTS));
    lv_label_set_text(s_lbl_tests_btn, i18n_t(STR_WIFI_CONFIGURE));
    lv_label_set_text(s_lbl_sync, i18n_t(STR_SETTINGS_SYNC));
    lv_label_set_text(s_lbl_sync_btn, i18n_t(STR_SETTINGS_SYNC));
}

// ── Helpers ──────────────────────────────────────────────────────────────────

static void save_settings(bool buzzer_on, bool vibrator_on, uint8_t volume) {
    // Note: i18n.c owns the authoritative language NVS key; this copy is
    // informational only.
    AppSettings s = {0};
    const char *lang = (i18n_get_language() == LANG_EN) ? "en" : "es";
    s.language[0] = lang[0];
    s.language[1] = lang[1];
    s.language[2] = '\0';
    s.buzzer_enabled = buzzer_on;
    s.vibrator_enabled = vibrator_on;
    s.speaker_volume = volume;
    nvs_save_settings(&s);
}

// ── Event callbacks ──────────────────────────────────────────────────────────

static void on_back(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SESSION_MENU); }

static void on_language(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    i18n_set_language(en ? LANG_EN : LANG_ES);
    ui_manager_refresh_language();
}

static void on_buzzer(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    feedback_set_buzzer_enabled(on);
    AppSettings s = {0};
    nvs_load_settings(&s);
    save_settings(on, s.vibrator_enabled, s.speaker_volume);
}

static void on_vibrator(lv_event_t *e) {
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    feedback_set_vibrator_enabled(on);
    AppSettings s = {0};
    nvs_load_settings(&s);
    save_settings(s.buzzer_enabled, on, s.speaker_volume);
}

static void on_brightness(lv_event_t *e) {
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    set_lcd_blight((uint32_t)val);
}

static void on_volume(lv_event_t *e) {
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    feedback_set_speaker_volume((uint8_t)val);
    AppSettings s = {0};
    nvs_load_settings(&s);
    save_settings(s.buzzer_enabled, s.vibrator_enabled, (uint8_t)val);
}

static void on_test_new_tag(lv_event_t *e) { (void)e; audio_test_new_tag(); }
static void on_test_duplicate(lv_event_t *e) { (void)e; audio_test_duplicate(); }

static void on_go_wifi(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_WIFI); }
static void on_go_vaccines(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_VACCINE_SETTINGS); }
static void on_go_tests(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_TEST_SETTINGS); }
static void on_go_sync(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_BLE_SYNC); }
static void on_go_datetime(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_DATETIME); }
static void on_go_mic_test(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_MIC_TEST); }

// Creates a single-row settings item container (label + control on one
// line) with an optional light bottom-border separator.
static lv_obj_t *make_settings_row(lv_obj_t *panel, int y, int h, int x, int w, bool border) {
    lv_obj_t *row = lv_obj_create(panel);
    lv_obj_set_size(row, w, h);
    lv_obj_set_pos(row, x, y);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    if (border) {
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, lv_color_hex(0xDDE1E7), LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    }
    return row;
}

// ── Screen creation ──────────────────────────────────────────────────────────

void screen_settings_create(void) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_text_font(s_screen, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // ── Fixed header (80px tall) ─────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_set_size(hdr, 480, 80);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x2c3e50), 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 70, 70);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 6);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_back, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_TITLE));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Scrollable row panel (below header) ──────────────────────────────────
    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_set_size(panel, 480, 720);
    lv_obj_set_pos(panel, 0, 80);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ACTIVE);

    // Each setting is a single row (label left, control right) so more items
    // fit on screen without scrolling; a light bottom border separates rows.
    int row_y = 12;
    const int row_h = 72;
    const int row_x = 24;
    const int row_w = 432; // 480 - 2*row_x
    const int btn_w = 170;
    const int btn_h = 46;
    const int sw_ext = 20;
    const int btn_ext = 12;
    const int sl_ext = 23;
    const lv_font_t *row_font = &lv_font_app_24;
    const lv_font_t *ctrl_font = &lv_font_app_20;

    // ── Language ─────────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_language = lv_label_create(row);
        lv_label_set_text(s_lbl_language, i18n_t(STR_SETTINGS_LANGUAGE));
        lv_obj_set_style_text_font(s_lbl_language, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_language, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(sw, sw_ext);
        if (i18n_get_language() == LANG_EN) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_language, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_t *lbl_ind = lv_label_create(row);
        lv_label_set_text(lbl_ind, "ES | EN");
        lv_obj_set_style_text_font(lbl_ind, ctrl_font, LV_PART_MAIN);
        lv_obj_align_to(lbl_ind, sw, LV_ALIGN_OUT_LEFT_MID, -10, 0);
        row_y += row_h;
    }

    // ── Buzzer ───────────────────────────────────────────────────────────────
    AppSettings saved = {0};
    nvs_load_settings(&saved);
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_buzzer = lv_label_create(row);
        lv_label_set_text(s_lbl_buzzer, i18n_t(STR_SETTINGS_BUZZER));
        lv_obj_set_style_text_font(s_lbl_buzzer, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_buzzer, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(sw, sw_ext);
        if (saved.buzzer_enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_buzzer, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    // ── Vibrator ─────────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_vibrator = lv_label_create(row);
        lv_label_set_text(s_lbl_vibrator, i18n_t(STR_SETTINGS_VIBRATOR));
        lv_obj_set_style_text_font(s_lbl_vibrator, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_vibrator, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *sw = lv_switch_create(row);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(sw, sw_ext);
        if (saved.vibrator_enabled) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_vibrator, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    // ── Brightness ───────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_brightness = lv_label_create(row);
        lv_label_set_text(s_lbl_brightness, i18n_t(STR_SETTINGS_BRIGHTNESS));
        lv_obj_set_style_text_font(s_lbl_brightness, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_brightness, LV_ALIGN_LEFT_MID, 0, 0);

        // Slider is a sibling of `row` (child of `panel`), not a child of it -
        // the row clips its children to its own box, which was cutting off
        // the slider knob's overhang at the right end of the track.
        const int slider_w = row_w - 150;
        const int slider_h = 24;
        lv_obj_t *slider = lv_slider_create(panel);
        lv_obj_set_size(slider, slider_w, slider_h);
        lv_obj_set_pos(slider, row_x + row_w - slider_w, row_y + (row_h - slider_h) / 2);
        lv_obj_set_ext_click_area(slider, sl_ext);
        lv_slider_set_range(slider, 20, 100);
        lv_slider_set_value(slider, 100, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    // ── Speaker Volume ───────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_volume = lv_label_create(row);
        lv_label_set_text(s_lbl_volume, i18n_t(STR_SETTINGS_VOLUME));
        lv_obj_set_style_text_font(s_lbl_volume, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_volume, LV_ALIGN_LEFT_MID, 0, 0);

        // Sibling of `row`, not a child — see the Brightness slider note above.
        const int slider_w = row_w - 150;
        const int slider_h = 24;
        lv_obj_t *slider = lv_slider_create(panel);
        lv_obj_set_size(slider, slider_w, slider_h);
        lv_obj_set_pos(slider, row_x + row_w - slider_w, row_y + (row_h - slider_h) / 2);
        lv_obj_set_ext_click_area(slider, sl_ext);
        lv_slider_set_range(slider, 0, 100);
        lv_slider_set_value(slider, saved.speaker_volume, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, on_volume, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    // ── Test Sounds ──────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_test_sounds = lv_label_create(row);
        lv_label_set_text(s_lbl_test_sounds, i18n_t(STR_SETTINGS_TEST_SOUNDS));
        lv_obj_set_style_text_font(s_lbl_test_sounds, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_test_sounds, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn_dup = lv_btn_create(row);
        lv_obj_set_size(btn_dup, 54, 54);
        lv_obj_align(btn_dup, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_opa(btn_dup, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_dup, LV_OPA_20, LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn_dup, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_dup, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(btn_dup, 6);
        lv_obj_add_event_cb(btn_dup, on_test_duplicate, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl_dup = lv_label_create(btn_dup);
        lv_label_set_text(lbl_dup, LV_SYMBOL_LOOP);
        lv_obj_set_style_text_font(lbl_dup, row_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_dup, lv_palette_main(LV_PALETTE_BLUE_GREY), LV_PART_MAIN);
        lv_obj_center(lbl_dup);

        lv_obj_t *btn_new = lv_btn_create(row);
        lv_obj_set_size(btn_new, 54, 54);
        lv_obj_align(btn_new, LV_ALIGN_RIGHT_MID, -64, 0);
        lv_obj_set_style_bg_opa(btn_new, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_new, LV_OPA_20, LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn_new, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_new, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(btn_new, 6);
        lv_obj_add_event_cb(btn_new, on_test_new_tag, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl_new = lv_label_create(btn_new);
        lv_label_set_text(lbl_new, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_font(lbl_new, row_font, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_new, lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
        lv_obj_center(lbl_new);
        row_y += row_h;
    }

    // ── Test Recording ───────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_mic_test = lv_label_create(row);
        lv_label_set_text(s_lbl_mic_test, i18n_t(STR_SETTINGS_MIC_TEST));
        lv_obj_set_style_text_font(s_lbl_mic_test, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_mic_test, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn_mic = lv_btn_create(row);
        lv_obj_set_size(btn_mic, btn_w, btn_h);
        lv_obj_align(btn_mic, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(btn_mic, btn_ext);
        lv_obj_add_event_cb(btn_mic, on_go_mic_test, LV_EVENT_CLICKED, NULL);
        s_lbl_mic_test_btn = lv_label_create(btn_mic);
        lv_label_set_text(s_lbl_mic_test_btn, i18n_t(STR_WIFI_CONFIGURE));
        lv_obj_set_style_text_font(s_lbl_mic_test_btn, ctrl_font, LV_PART_MAIN);
        lv_obj_center(s_lbl_mic_test_btn);
        row_y += row_h;
    }

    // ── Date & Time ──────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_datetime = lv_label_create(row);
        lv_label_set_text(s_lbl_datetime, i18n_t(STR_SETTINGS_DATETIME));
        lv_obj_set_style_text_font(s_lbl_datetime, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_datetime, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn_edit = lv_btn_create(row);
        lv_obj_set_size(btn_edit, btn_w, btn_h);
        lv_obj_align(btn_edit, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(btn_edit, btn_ext);
        lv_obj_add_event_cb(btn_edit, on_go_datetime, LV_EVENT_CLICKED, NULL);
        s_lbl_datetime_btn = lv_label_create(btn_edit);
        lv_label_set_text(s_lbl_datetime_btn, i18n_t(STR_SETTINGS_SET_TIME));
        lv_obj_set_style_text_font(s_lbl_datetime_btn, ctrl_font, LV_PART_MAIN);
        lv_obj_center(s_lbl_datetime_btn);
        row_y += row_h;
    }

    // ── WiFi ─────────────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_wifi = lv_label_create(row);
        lv_label_set_text(s_lbl_wifi, i18n_t(STR_SETTINGS_WIFI));
        lv_obj_set_style_text_font(s_lbl_wifi, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_wifi, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn_wifi = lv_btn_create(row);
        lv_obj_set_size(btn_wifi, btn_w, btn_h);
        lv_obj_align(btn_wifi, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(btn_wifi, btn_ext);
        lv_obj_add_event_cb(btn_wifi, on_go_wifi, LV_EVENT_CLICKED, NULL);
        s_lbl_wifi_btn = lv_label_create(btn_wifi);
        lv_label_set_text(s_lbl_wifi_btn, i18n_t(STR_WIFI_CONFIGURE));
        lv_obj_set_style_text_font(s_lbl_wifi_btn, ctrl_font, LV_PART_MAIN);
        lv_obj_center(s_lbl_wifi_btn);
        row_y += row_h;
    }

    // ── Vaccines ─────────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_vaccines = lv_label_create(row);
        lv_label_set_text(s_lbl_vaccines, i18n_t(STR_SETTINGS_VACCINES));
        lv_obj_set_style_text_font(s_lbl_vaccines, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_vaccines, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn_vax = lv_btn_create(row);
        lv_obj_set_size(btn_vax, btn_w, btn_h);
        lv_obj_align(btn_vax, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(btn_vax, btn_ext);
        lv_obj_add_event_cb(btn_vax, on_go_vaccines, LV_EVENT_CLICKED, NULL);
        s_lbl_vaccines_btn = lv_label_create(btn_vax);
        lv_label_set_text(s_lbl_vaccines_btn, i18n_t(STR_WIFI_CONFIGURE));
        lv_obj_set_style_text_font(s_lbl_vaccines_btn, ctrl_font, LV_PART_MAIN);
        lv_obj_center(s_lbl_vaccines_btn);
        row_y += row_h;
    }

    // ── Tests ────────────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, true);
        s_lbl_tests = lv_label_create(row);
        lv_label_set_text(s_lbl_tests, i18n_t(STR_SETTINGS_TESTS));
        lv_obj_set_style_text_font(s_lbl_tests, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_tests, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn_tests = lv_btn_create(row);
        lv_obj_set_size(btn_tests, btn_w, btn_h);
        lv_obj_align(btn_tests, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(btn_tests, btn_ext);
        lv_obj_add_event_cb(btn_tests, on_go_tests, LV_EVENT_CLICKED, NULL);
        s_lbl_tests_btn = lv_label_create(btn_tests);
        lv_label_set_text(s_lbl_tests_btn, i18n_t(STR_WIFI_CONFIGURE));
        lv_obj_set_style_text_font(s_lbl_tests_btn, ctrl_font, LV_PART_MAIN);
        lv_obj_center(s_lbl_tests_btn);
        row_y += row_h;
    }

    // ── Sync to PC ───────────────────────────────────────────────────────────
    {
        lv_obj_t *row = make_settings_row(panel, row_y, row_h, row_x, row_w, false);
        s_lbl_sync = lv_label_create(row);
        lv_label_set_text(s_lbl_sync, i18n_t(STR_SETTINGS_SYNC));
        lv_obj_set_style_text_font(s_lbl_sync, row_font, LV_PART_MAIN);
        lv_obj_align(s_lbl_sync, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *btn_sync = lv_btn_create(row);
        lv_obj_set_size(btn_sync, btn_w, btn_h);
        lv_obj_align(btn_sync, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_ext_click_area(btn_sync, btn_ext);
        lv_obj_add_event_cb(btn_sync, on_go_sync, LV_EVENT_CLICKED, NULL);
        s_lbl_sync_btn = lv_label_create(btn_sync);
        lv_label_set_text(s_lbl_sync_btn, i18n_t(STR_SETTINGS_SYNC));
        lv_obj_set_style_text_font(s_lbl_sync_btn, ctrl_font, LV_PART_MAIN);
        lv_obj_center(s_lbl_sync_btn);
        row_y += row_h;
    }

    // ── Version (inside panel, scrolls with content) ─────────────────────────
    {
        lv_obj_t *lbl = lv_label_create(panel);
        lv_label_set_text(lbl, "Pilocows v" FIRMWARE_VERSION);
        lv_obj_set_style_text_font(lbl, &lv_font_app_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
        lv_obj_set_pos(lbl, 0, row_y + 15);
        lv_obj_set_width(lbl, 480);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

void screen_settings_load(void) {
    lv_scr_load(s_screen);
}

void screen_settings_refresh_language(void) {
    refresh_language();
}
