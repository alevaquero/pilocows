#include "screen_session_list.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "ui_text_entry.h"
#include "session_storage.h"
#include "i18n.h"
#include "strings_en.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

static const char *TAG = "scr_sess_list";

#define LIST_MAX 50

// ── Static label refs ─────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_back;
static lv_obj_t *s_lbl_empty;

// ── List widget ───────────────────────────────────────────────────────────────
static lv_obj_t *s_list;

// ── Delete confirmation popup ─────────────────────────────────────────────────
static lv_obj_t *s_confirm_panel;
static lv_obj_t *s_lbl_confirm_msg;
static lv_obj_t *s_lbl_confirm_ok;
static lv_obj_t *s_lbl_confirm_cancel;
static uint32_t s_confirm_id = 0;

static lv_obj_t *s_scr = NULL;

// Rows reference this buffer via user_data — persists across redraws.
static session_meta_t s_copy_buf[LIST_MAX];

static void rebuild_list(void);

// Creates a flat icon button (transparent bg, coloured symbol) inside parent.
static lv_obj_t *make_icon_btn(lv_obj_t *parent, const char *symbol, lv_color_t color) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 54, 54);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20, LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn, 6);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_center(lbl);
    return btn;
}

static void on_row_activate(lv_event_t *e) {
    session_meta_t *meta = (session_meta_t *)lv_event_get_user_data(e);
    session_set_active(meta->id);
    ESP_LOGI(TAG, "Set current session %" PRIu32, meta->id);
    ui_manager_show(SCREEN_SCAN);
}

static void on_note_confirm(const char *text, void *user_data) {
    uint32_t id = (uint32_t)(uintptr_t)user_data;
    session_save_note(id, text);
    rebuild_list();
}

static void on_row_edit(lv_event_t *e) {
    session_meta_t *meta = (session_meta_t *)lv_event_get_user_data(e);
    ui_text_entry_cfg_t cfg = {
        .label = i18n_t(STR_SESSION_NOTE),
        .initial_text = meta->note,
        .placeholder = i18n_t(STR_SESSION_NOTE_PLACEHOLDER),
        .multiline = true,
        .password = false,
        .max_length = SESSION_NOTE_MAX - 1,
        .on_confirm = on_note_confirm,
        .on_cancel = NULL,
        .user_data = (void *)(uintptr_t)meta->id,
    };
    ui_text_entry_show(&cfg);
}

static void on_row_delete(lv_event_t *e) {
    session_meta_t *meta = (session_meta_t *)lv_event_get_user_data(e);
    s_confirm_id = meta->id;
    lv_obj_clear_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
}

static void rebuild_list(void) {
    lv_obj_clean(s_list);

    session_meta_t sessions[LIST_MAX];
    int count = (int)session_list(sessions, LIST_MAX);

    if (count == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < count; i++) {
        s_copy_buf[i] = sessions[i];
        const session_meta_t *m = &s_copy_buf[i];

        // Two-line row: name (with "..." truncation) on top, "Scanned: N" +
        // icon buttons below. Tapping anywhere on the row (outside the icon
        // buttons) activates the session, same as pressing the play icon.
        lv_obj_t *row = lv_list_add_btn(s_list, NULL, "");
        lv_obj_set_height(row, 110);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_layout(row, 0);
        lv_obj_add_event_cb(row, on_row_activate, LV_EVENT_CLICKED, &s_copy_buf[i]);

        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, m->name);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_set_size(lbl_name, 460, 34);
        lv_obj_set_style_text_font(lbl_name, &lv_font_app_28, LV_PART_MAIN);
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 10, 8);

        char cnt_buf[48];
        snprintf(cnt_buf, sizeof(cnt_buf), "%s %" PRIu32, i18n_t(STR_SCAN_COUNT_LABEL), m->tag_count);
        lv_obj_t *lbl_cnt = lv_label_create(row);
        lv_label_set_text(lbl_cnt, cnt_buf);
        lv_obj_set_style_text_font(lbl_cnt, &lv_font_app_20, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_cnt, lv_palette_main(LV_PALETTE_BLUE_GREY), LV_PART_MAIN);
        lv_obj_align(lbl_cnt, LV_ALIGN_BOTTOM_LEFT, 10, -8);

        lv_obj_t *btn_play = make_icon_btn(row, LV_SYMBOL_PLAY, lv_palette_main(LV_PALETTE_BLUE));
        lv_obj_align(btn_play, LV_ALIGN_BOTTOM_RIGHT, -140, -8);
        lv_obj_add_event_cb(btn_play, on_row_activate, LV_EVENT_CLICKED, &s_copy_buf[i]);

        lv_obj_t *btn_edit = make_icon_btn(row, LV_SYMBOL_EDIT, lv_palette_main(LV_PALETTE_BLUE_GREY));
        lv_obj_align(btn_edit, LV_ALIGN_BOTTOM_RIGHT, -73, -8);
        lv_obj_add_event_cb(btn_edit, on_row_edit, LV_EVENT_CLICKED, &s_copy_buf[i]);

        lv_obj_t *btn_trash = make_icon_btn(row, LV_SYMBOL_TRASH, lv_palette_main(LV_PALETTE_RED));
        lv_obj_align(btn_trash, LV_ALIGN_BOTTOM_RIGHT, -7, -8);
        lv_obj_add_event_cb(btn_trash, on_row_delete, LV_EVENT_CLICKED, &s_copy_buf[i]);
    }
}

// ── Callbacks ────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SESSION_MENU); }

static void on_confirm_delete(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_confirm_id == 0) return;
    esp_err_t err = session_delete(s_confirm_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Deleted session %" PRIu32, s_confirm_id);
    } else {
        ESP_LOGE(TAG, "Delete session %" PRIu32 " failed: %s", s_confirm_id, esp_err_to_name(err));
    }
    s_confirm_id = 0;
    rebuild_list();
}

static void on_confirm_cancel(lv_event_t *e) {
    (void)e;
    s_confirm_id = 0;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    rebuild_list();
}

void screen_session_list_create(void) {
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header ──────────────────────────────────────────────────────────────
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
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_back, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_LIST));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Empty label ───────────────────────────────────────────────────────────
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, i18n_t(STR_HISTORY_EMPTY));
    lv_obj_set_style_text_font(s_lbl_empty, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_empty, 0, 225);
    lv_obj_set_width(s_lbl_empty, 480);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    // ── Scrollable list ───────────────────────────────────────────────────────
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, 480, 720);
    lv_obj_set_pos(s_list, 0, 80);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 6, LV_PART_MAIN);

    // ── Delete confirmation popup ─────────────────────────────────────────────
    s_confirm_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_confirm_panel, 480, 800);
    lv_obj_set_pos(s_confirm_panel, 0, 0);
    lv_obj_clear_flag(s_confirm_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_confirm_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_confirm_panel, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_confirm_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_confirm_panel, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(s_confirm_panel);
    lv_obj_set_size(card, 420, 260);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 24, LV_PART_MAIN);

    s_lbl_confirm_msg = lv_label_create(card);
    lv_label_set_text(s_lbl_confirm_msg, i18n_t(STR_SESSION_CONFIRM_DELETE));
    lv_obj_set_style_text_font(s_lbl_confirm_msg, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_width(s_lbl_confirm_msg, 372);
    lv_label_set_long_mode(s_lbl_confirm_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_lbl_confirm_msg, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn_cancel = lv_btn_create(card);
    lv_obj_set_size(btn_cancel, 180, 66);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_cancel, 10);
    lv_obj_add_event_cb(btn_cancel, on_confirm_cancel, LV_EVENT_CLICKED, NULL);
    s_lbl_confirm_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(s_lbl_confirm_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_font(s_lbl_confirm_cancel, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_confirm_cancel, lv_color_black(), LV_PART_MAIN);
    lv_obj_center(s_lbl_confirm_cancel);

    lv_obj_t *btn_ok = lv_btn_create(card);
    lv_obj_set_size(btn_ok, 180, 66);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_ok, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_ok, 10);
    lv_obj_add_event_cb(btn_ok, on_confirm_delete, LV_EVENT_CLICKED, NULL);
    s_lbl_confirm_ok = lv_label_create(btn_ok);
    lv_label_set_text(s_lbl_confirm_ok, i18n_t(STR_SESSION_DELETE));
    lv_obj_set_style_text_font(s_lbl_confirm_ok, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_confirm_ok, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(s_lbl_confirm_ok);

    ESP_LOGI(TAG, "Session list screen created");
}

void screen_session_list_load(void) {
    lv_scr_load(s_scr);
    // rebuild_list() and overlay resets run via on_screen_loaded
}

void screen_session_list_refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_LIST));
    lv_label_set_text(s_lbl_confirm_msg, i18n_t(STR_SESSION_CONFIRM_DELETE));
    lv_label_set_text(s_lbl_confirm_ok, i18n_t(STR_SESSION_DELETE));
    lv_label_set_text(s_lbl_confirm_cancel, i18n_t(STR_BTN_CANCEL));
    rebuild_list();
}
