#include "screen_session_menu.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "session_storage.h"
#include "i18n.h"
#include "strings_en.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "scr_sess_menu";

// ── Static label refs (for refresh_language) ─────────────────────────────────
static lv_obj_t *s_lbl_no_session;
static lv_obj_t *s_lbl_btn_resume;
static lv_obj_t *s_lbl_btn_new;
static lv_obj_t *s_lbl_btn_list;
static lv_obj_t *s_lbl_btn_settings;

// ── Dynamic content labels ────────────────────────────────────────────────────
static lv_obj_t *s_lbl_sess_name;
static lv_obj_t *s_lbl_sess_type;
static lv_obj_t *s_lbl_sess_count;

// ── Layout containers toggled by active-session state ────────────────────────
static lv_obj_t *s_card;
static lv_obj_t *s_row_resume;

// ── Screen root ───────────────────────────────────────────────────────────────
static lv_obj_t *s_scr = NULL;

// Returns the English string literal for a session type (for i18n_t lookup).
static const char *type_en_str(session_type_t type) {
    switch (type) {
        case SESSION_TYPE_WEIGHING:    return STR_EVENT_WEIGHING;
        case SESSION_TYPE_VACCINATION: return STR_EVENT_VACCINATION;
        case SESSION_TYPE_PREGNANCY:   return STR_EVENT_PREGNANCY;
        case SESSION_TYPE_TEST:        return STR_EVENT_TEST;
        case SESSION_TYPE_REMOVAL:     return STR_EVENT_REMOVAL;
        default:                       return STR_EVENT_GENERAL;
    }
}

static void refresh_state(void) {
    session_meta_t m;
    bool has_active = session_get_active(&m);

    if (has_active) {
        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%s %" PRIu32, i18n_t(STR_SCAN_COUNT_LABEL), m.tag_count);

        lv_label_set_text(s_lbl_sess_name, m.name);
        lv_label_set_text(s_lbl_sess_type, i18n_t(type_en_str(m.type)));
        lv_label_set_text(s_lbl_sess_count, count_str);

        lv_obj_clear_flag(s_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_row_resume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_no_session, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_row_resume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lbl_no_session, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_settings(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SETTINGS); }
static void on_resume(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SCAN); }
static void on_new_session(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SESSION_NEW); }
static void on_session_list(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SESSION_LIST); }

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "on_screen_loaded: calling refresh_state()");
    refresh_state();
    ESP_LOGI(TAG, "on_screen_loaded: refresh_state() returned");
}

void screen_session_menu_create(void) {
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Layout (480x800, no header) — evenly spaced (60px gaps) so Settings
    // lands flush at the bottom with the same 13px margin used on the sides:
    //   y= 12  Active session card       (454x190 — name/type/count, one per line)
    //   y=169  "No active session" label (centered, shown when no card)
    //   y=262  Resume row                (454x105, hidden when no active session)
    //   y=427  New + List rows           (454x105 each, stacked, always visible)
    //   y=709  Settings button           (454x78, always visible, flush to bottom)

    s_card = lv_obj_create(s_scr);
    lv_obj_set_size(s_card, 454, 190);
    lv_obj_set_pos(s_card, 13, 12);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_card, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_card, 15, LV_PART_MAIN);

    // Name / type / tag count, each centered on its own line (was two
    // left-aligned lines cramming "type | count" together — doesn't read well
    // on the narrower portrait card).
    s_lbl_sess_name = lv_label_create(s_card);
    lv_label_set_text(s_lbl_sess_name, "");
    lv_obj_set_style_text_font(s_lbl_sess_name, &lv_font_app_30, LV_PART_MAIN);
    lv_label_set_long_mode(s_lbl_sess_name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_sess_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // Full name always shown, wrapping to up to 2 lines (fixed height reserves
    // room for both so type/count below never overlap it).
    lv_obj_set_size(s_lbl_sess_name, 424, 76);
    lv_obj_align(s_lbl_sess_name, LV_ALIGN_TOP_MID, 0, 8);

    s_lbl_sess_type = lv_label_create(s_card);
    lv_label_set_text(s_lbl_sess_type, "");
    lv_obj_set_style_text_font(s_lbl_sess_type, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_width(s_lbl_sess_type, 424);
    lv_obj_set_style_text_align(s_lbl_sess_type, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_lbl_sess_type, LV_ALIGN_TOP_MID, 0, 90);

    s_lbl_sess_count = lv_label_create(s_card);
    lv_label_set_text(s_lbl_sess_count, "");
    lv_obj_set_style_text_font(s_lbl_sess_count, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_sess_count, lv_palette_main(LV_PALETTE_BLUE_GREY), LV_PART_MAIN);
    lv_obj_set_width(s_lbl_sess_count, 424);
    lv_obj_set_style_text_align(s_lbl_sess_count, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_lbl_sess_count, LV_ALIGN_TOP_MID, 0, 123);

    // ── Resume button (full-width) ────────────────────────────────────────
    s_row_resume = lv_obj_create(s_scr);
    lv_obj_set_size(s_row_resume, 454, 105);
    lv_obj_set_pos(s_row_resume, 13, 262);
    lv_obj_clear_flag(s_row_resume, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_row_resume, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_row_resume, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_row_resume, 0, LV_PART_MAIN);

    lv_obj_t *btn_resume = lv_btn_create(s_row_resume);
    lv_obj_set_size(btn_resume, 454, 105);
    lv_obj_set_pos(btn_resume, 0, 0);
    lv_obj_set_style_radius(btn_resume, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_resume, lv_color_hex(0x27AE60), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_resume, 10);
    lv_obj_add_event_cb(btn_resume, on_resume, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_resume = lv_label_create(btn_resume);
    lv_label_set_text(s_lbl_btn_resume, i18n_t(STR_SESSION_RESUME));
    lv_obj_set_style_text_color(s_lbl_btn_resume, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_resume, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_resume);

    // ── "No active session" label ─────────────────────────────────────────
    s_lbl_no_session = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SESSION_NONE));
    lv_obj_set_style_text_font(s_lbl_no_session, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_no_session, 0, 169);
    lv_obj_set_width(s_lbl_no_session, 480);
    lv_obj_set_style_text_align(s_lbl_no_session, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // ── New + List rows (always visible) ──────────────────────────────────
    // Stacked (was side-by-side) since 375w+385w doesn't fit 480px width.
    lv_obj_t *row_always = lv_obj_create(s_scr);
    lv_obj_set_size(row_always, 454, 222);
    lv_obj_set_pos(row_always, 13, 427);
    lv_obj_clear_flag(row_always, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(row_always, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row_always, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row_always, 0, LV_PART_MAIN);

    lv_obj_t *btn_new = lv_btn_create(row_always);
    lv_obj_set_size(btn_new, 454, 105);
    lv_obj_set_pos(btn_new, 0, 0);
    lv_obj_set_style_radius(btn_new, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_new, 10);
    lv_obj_add_event_cb(btn_new, on_new_session, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_new = lv_label_create(btn_new);
    lv_label_set_text(s_lbl_btn_new, i18n_t(STR_SESSION_NEW));
    lv_obj_set_style_text_font(s_lbl_btn_new, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_new);

    lv_obj_t *btn_list = lv_btn_create(row_always);
    lv_obj_set_size(btn_list, 454, 105);
    lv_obj_set_pos(btn_list, 0, 117);
    lv_obj_set_style_radius(btn_list, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_list, 10);
    lv_obj_add_event_cb(btn_list, on_session_list, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_list = lv_label_create(btn_list);
    lv_label_set_text(s_lbl_btn_list, i18n_t(STR_SESSION_LIST));
    lv_obj_set_style_text_font(s_lbl_btn_list, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_list);

    // ── Settings button (full-width) ────────────────────────────────────────
    lv_obj_t *btn_settings = lv_btn_create(s_scr);
    lv_obj_set_size(btn_settings, 454, 78);
    lv_obj_set_pos(btn_settings, 13, 709);
    lv_obj_set_style_radius(btn_settings, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x607D8B), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_settings, 20);
    lv_obj_add_event_cb(btn_settings, on_settings, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_settings = lv_label_create(btn_settings);
    lv_label_set_text_fmt(s_lbl_btn_settings, "%s  %s", LV_SYMBOL_SETTINGS, i18n_t(STR_SETTINGS_TITLE));
    lv_obj_set_style_text_color(s_lbl_btn_settings, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_settings, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_settings);

    // ── Initial state (hidden until load) ───────────────────────────────────
    lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_row_resume, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_no_session, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Session menu screen created");
}

void screen_session_menu_load(void) {
    lv_scr_load(s_scr);
    // refresh_state() runs via on_screen_loaded event
}

void screen_session_menu_refresh_language(void) {
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SESSION_NONE));
    lv_label_set_text(s_lbl_btn_resume, i18n_t(STR_SESSION_RESUME));
    lv_label_set_text(s_lbl_btn_new, i18n_t(STR_SESSION_NEW));
    lv_label_set_text(s_lbl_btn_list, i18n_t(STR_SESSION_LIST));
    lv_label_set_text_fmt(s_lbl_btn_settings, "%s  %s", LV_SYMBOL_SETTINGS, i18n_t(STR_SETTINGS_TITLE));
    refresh_state();
}
