#include "screen_session_menu.h"
#include "ui_manager.h"
#include "lvgl.h"
#include "../i18n/i18n.h"
#include "../i18n/strings_en.h"
#include "../storage/session_storage.h"
#include "esp_log.h"
#include <stdio.h>
#include <inttypes.h>

static const char *TAG = "scr_sess_menu";

// ── Static label refs (for refresh_language) ─────────────────────────────────
static lv_obj_t *s_lbl_no_session;
static lv_obj_t *s_lbl_btn_resume;
static lv_obj_t *s_lbl_btn_close;
static lv_obj_t *s_lbl_btn_new;
static lv_obj_t *s_lbl_btn_list;
static lv_obj_t *s_lbl_btn_settings;

// ── Dynamic content labels (refreshed from storage) ───────────────────────────
static lv_obj_t *s_lbl_sess_name;
static lv_obj_t *s_lbl_sess_info;  // "Weighing · 5 animals · Open"

// ── Layout containers toggled by active-session state ────────────────────────
static lv_obj_t *s_card;            // active session card
static lv_obj_t *s_row_resume;      // Resume + Close buttons row
static lv_obj_t *s_row_always;      // New + List buttons (always visible)

// ── Screen root ───────────────────────────────────────────────────────────────
static lv_obj_t *s_scr = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Returns the English string literal for a session type (for i18n_t lookup).
static const char *type_en_str(uint8_t type)
{
    switch ((session_type_t)type) {
        case SESSION_TYPE_WEIGHING:    return "Weighing";
        case SESSION_TYPE_VACCINATION: return "Vaccination";
        case SESSION_TYPE_PREGNANCY:   return "Pregnancy Check";
        case SESSION_TYPE_TB_TEST:     return "TB Test";
        case SESSION_TYPE_REMOVAL:     return "Removal";
        default:                       return "General";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// State refresh — show/hide card and update dynamic content
// ─────────────────────────────────────────────────────────────────────────────

static void refresh_state(void)
{
    session_meta_t m;
    bool has_active = session_get_active(&m);

    if (has_active) {
        // Build info string: "Weighing · 12 · Open"
        const char *type_str   = i18n_t(type_en_str(m.type));
        const char *status_str = (m.status == SESSION_STATUS_OPEN)
                                 ? i18n_t("Open") : i18n_t("Closed");
        char info[64];
        snprintf(info, sizeof(info), "%s | %" PRIu32 " | %s",
                 type_str, m.tag_count, status_str);

        lv_label_set_text(s_lbl_sess_name, m.name);
        lv_label_set_text(s_lbl_sess_info, info);

        lv_obj_clear_flag(s_card,       LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_row_resume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (s_lbl_no_session, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag  (s_card,       LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag  (s_row_resume, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lbl_no_session, LV_OBJ_FLAG_HIDDEN);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_settings(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SETTINGS);
}

static void on_resume(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SCAN);
}

static void on_close_session(lv_event_t *e)
{
    (void)e;
    session_meta_t m;
    if (session_get_active(&m)) {
        session_set_status(m.id, SESSION_STATUS_CLOSED);
        ESP_LOGI(TAG, "Session %" PRIu32 " closed", m.id);
    }
    refresh_state();
}

static void on_new_session(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SESSION_NEW);
}

static void on_session_list(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SESSION_LIST);
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;
    refresh_state();
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_menu_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Layout (480×320, no header)
    //   y=  8  Active session card       (464×90)
    //   y= 44  "No active session" label (centered, shown when no card)
    //   y=106  Resume + Close row        (464×70, hidden when no active session)
    //   y=182  New + List row            (464×70, always visible)
    //   y=260  Settings button           (464×52, always visible, bottom)

    // ── Active session card ───────────────────────────────────────────────────
    s_card = lv_obj_create(s_scr);
    lv_obj_set_size(s_card, 464, 90);
    lv_obj_set_pos(s_card, 8, 8);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_card, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_card, 10, LV_PART_MAIN);

    s_lbl_sess_name = lv_label_create(s_card);
    lv_label_set_text(s_lbl_sess_name, "");
    lv_obj_set_style_text_font(s_lbl_sess_name, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_label_set_long_mode(s_lbl_sess_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_sess_name, 444);
    lv_obj_align(s_lbl_sess_name, LV_ALIGN_TOP_LEFT, 0, 0);

    s_lbl_sess_info = lv_label_create(s_card);
    lv_label_set_text(s_lbl_sess_info, "");
    lv_obj_set_style_text_font(s_lbl_sess_info, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_sess_info, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    // ── Resume + Close row ────────────────────────────────────────────────────
    s_row_resume = lv_obj_create(s_scr);
    lv_obj_set_size(s_row_resume, 464, 70);
    lv_obj_set_pos(s_row_resume, 8, 106);
    lv_obj_clear_flag(s_row_resume, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_row_resume, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_row_resume, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_row_resume, 0, LV_PART_MAIN);

    lv_obj_t *btn_resume = lv_btn_create(s_row_resume);
    lv_obj_set_size(btn_resume, 225, 70);
    lv_obj_set_pos(btn_resume, 0, 0);
    lv_obj_set_style_radius(btn_resume, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_resume, lv_color_hex(0x27AE60), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_resume, 10);
    lv_obj_add_event_cb(btn_resume, on_resume, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_resume = lv_label_create(btn_resume);
    lv_label_set_text(s_lbl_btn_resume, i18n_t(STR_SESSION_RESUME));
    lv_obj_set_style_text_color(s_lbl_btn_resume, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_resume, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_resume);

    lv_obj_t *btn_close = lv_btn_create(s_row_resume);
    lv_obj_set_size(btn_close, 231, 70);
    lv_obj_set_pos(btn_close, 233, 0);
    lv_obj_set_style_radius(btn_close, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_close, lv_color_hex(0xE67E22), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_close, 10);
    lv_obj_add_event_cb(btn_close, on_close_session, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_close = lv_label_create(btn_close);
    lv_label_set_text(s_lbl_btn_close, i18n_t(STR_SESSION_CLOSE));
    lv_obj_set_style_text_color(s_lbl_btn_close, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_close, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_close);

    // ── "No active session" label ─────────────────────────────────────────────
    s_lbl_no_session = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SESSION_NONE));
    lv_obj_set_style_text_font(s_lbl_no_session, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_no_session, 0, 44);
    lv_obj_set_width(s_lbl_no_session, 480);
    lv_obj_set_style_text_align(s_lbl_no_session, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // ── New + List row (always visible) ───────────────────────────────────────
    // y=182: 6px gap below resume row (y=106 h=70 → bottom=176)
    s_row_always = lv_obj_create(s_scr);
    lv_obj_set_size(s_row_always, 464, 70);
    lv_obj_set_pos(s_row_always, 8, 182);
    lv_obj_clear_flag(s_row_always, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_row_always, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_row_always, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_row_always, 0, LV_PART_MAIN);

    lv_obj_t *btn_new = lv_btn_create(s_row_always);
    lv_obj_set_size(btn_new, 225, 70);
    lv_obj_set_pos(btn_new, 0, 0);
    lv_obj_set_style_radius(btn_new, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_new, 10);
    lv_obj_add_event_cb(btn_new, on_new_session, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_new = lv_label_create(btn_new);
    lv_label_set_text(s_lbl_btn_new, i18n_t(STR_SESSION_NEW));
    lv_obj_set_style_text_font(s_lbl_btn_new, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_new);

    lv_obj_t *btn_list = lv_btn_create(s_row_always);
    lv_obj_set_size(btn_list, 231, 70);
    lv_obj_set_pos(btn_list, 233, 0);
    lv_obj_set_style_radius(btn_list, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_list, 10);
    lv_obj_add_event_cb(btn_list, on_session_list, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_list = lv_label_create(btn_list);
    lv_label_set_text(s_lbl_btn_list, i18n_t(STR_SESSION_LIST));
    lv_obj_set_style_text_font(s_lbl_btn_list, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_list);

    // ── Settings button (full-width, anchored to bottom) ──────────────────────
    lv_obj_t *btn_settings = lv_btn_create(s_scr);
    lv_obj_set_size(btn_settings, 464, 52);
    lv_obj_set_pos(btn_settings, 8, 260);
    lv_obj_set_style_radius(btn_settings, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x607D8B), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_settings, 10);
    lv_obj_add_event_cb(btn_settings, on_settings, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_settings = lv_label_create(btn_settings);
    lv_label_set_text_fmt(s_lbl_btn_settings, "%s  %s", LV_SYMBOL_SETTINGS, i18n_t(STR_SETTINGS_TITLE));
    lv_obj_set_style_text_color(s_lbl_btn_settings, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_settings, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_settings);

    // ── Initial state (hidden until load) ─────────────────────────────────────
    lv_obj_add_flag(s_card,           LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_row_resume,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_no_session, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Session menu screen created");
}

// ─────────────────────────────────────────────────────────────────────────────
// Load / refresh
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_menu_load(void)
{
    lv_scr_load(s_scr);
    // refresh_state() runs via on_screen_loaded event
}

void screen_session_menu_refresh_language(void)
{
    lv_label_set_text(s_lbl_no_session,    i18n_t(STR_SESSION_NONE));
    lv_label_set_text(s_lbl_btn_resume,    i18n_t(STR_SESSION_RESUME));
    lv_label_set_text(s_lbl_btn_close,     i18n_t(STR_SESSION_CLOSE));
    lv_label_set_text(s_lbl_btn_new,       i18n_t(STR_SESSION_NEW));
    lv_label_set_text(s_lbl_btn_list,      i18n_t(STR_SESSION_LIST));
    lv_label_set_text_fmt(s_lbl_btn_settings, "%s  %s", LV_SYMBOL_SETTINGS, i18n_t(STR_SETTINGS_TITLE));
    // Rebuild dynamic content so type/status strings pick up new language
    refresh_state();
}
