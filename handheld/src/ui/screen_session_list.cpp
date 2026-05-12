#include "screen_session_list.h"
#include "ui_manager.h"
#include "lvgl.h"
#include "../i18n/i18n.h"
#include "../i18n/strings_en.h"
#include "../storage/session_storage.h"
#include "esp_log.h"
#include <stdio.h>
#include <inttypes.h>
#include "fonts.h"

static const char *TAG = "scr_sess_list";

#define LIST_MAX 50

// ── Static label refs ─────────────────────────────────────────────────────────
static lv_obj_t *s_hdr;
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
static uint32_t  s_confirm_id = 0;

// ── Note edit modal ────────────────────────────────────────────────────────────
static lv_obj_t *s_note_overlay;
static lv_obj_t *s_ta_note_edit;
static lv_obj_t *s_kb_note_edit;

// Currently selected session for note editing
static uint32_t s_selected_id   = 0;
static char     s_selected_note[SESSION_NOTE_MAX] = {0};

static lv_obj_t *s_scr = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static const char *type_en_str(uint8_t type)
{
    switch ((session_type_t)type) {
        case SESSION_TYPE_WEIGHING:    return "Weighing";
        case SESSION_TYPE_VACCINATION: return "Vaccination";
        case SESSION_TYPE_PREGNANCY:   return "Pregnancy Check";
        case SESSION_TYPE_TEST:        return "Test";
        case SESSION_TYPE_REMOVAL:     return "Removal";
        default:                       return "General";
    }
}

static void open_note_modal(void);
static void close_note_modal(void);

// Creates a flat icon button (transparent bg, coloured symbol) inside parent.
// Returns the button object; caller must align and add event callback.
static lv_obj_t *make_icon_btn(lv_obj_t *parent, const char *symbol, lv_color_t color)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 36, 36);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_20,     LV_STATE_PRESSED | LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn, 4);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_center(lbl);
    return btn;
}

static void rebuild_list(void)
{
    lv_obj_clean(s_list);

    static session_meta_t sessions[LIST_MAX];
    int count = session_list_all(sessions, LIST_MAX);

    if (count == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    // copy_buf lives for the lifetime of the list widgets — captured as user_data
    // by the per-row callbacks. Static so it persists across redraws.
    static session_meta_t copy_buf[LIST_MAX];

    for (int i = 0; i < count; i++) {
        copy_buf[i] = sessions[i];
        const session_meta_t *m = &copy_buf[i];

        lv_obj_t *row = lv_list_add_btn(s_list, NULL, "");
        lv_obj_set_height(row, 48);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_layout(row, 0);  // LV_LAYOUT_NONE — manual placement

        // ── Session name ──────────────────────────────────────────────────────
        lv_obj_t *lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, m->name);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl_name, 200);
        lv_obj_set_style_text_font(lbl_name, &pilocows_font_18, LV_PART_MAIN);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 6, 0);

        // ── Tag count ─────────────────────────────────────────────────────────
        char cnt_buf[12];
        snprintf(cnt_buf, sizeof(cnt_buf), "%" PRIu32, m->tag_count);
        lv_obj_t *lbl_cnt = lv_label_create(row);
        lv_label_set_text(lbl_cnt, cnt_buf);
        lv_obj_set_style_text_font(lbl_cnt, &pilocows_font_18, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl_cnt, lv_palette_main(LV_PALETTE_BLUE_GREY), LV_PART_MAIN);
        // Sits between name and the three icon buttons
        lv_obj_align(lbl_cnt, LV_ALIGN_RIGHT_MID, -126, 0);

        // ── Activate (Set as Current) — LV_SYMBOL_PLAY ────────────────────────
        lv_obj_t *btn_play = make_icon_btn(row, LV_SYMBOL_PLAY,
                                           lv_palette_main(LV_PALETTE_BLUE));
        lv_obj_align(btn_play, LV_ALIGN_RIGHT_MID, -84, 0);
        lv_obj_add_event_cb(btn_play, [](lv_event_t *ev) {
            session_meta_t *meta = (session_meta_t *)lv_event_get_user_data(ev);
            session_set_active(meta->id);
            ESP_LOGI(TAG, "Set current session %" PRIu32, meta->id);
            ui_manager_show(SCREEN_SCAN);
        }, LV_EVENT_CLICKED, &copy_buf[i]);

        // ── Edit note — LV_SYMBOL_EDIT ────────────────────────────────────────
        lv_obj_t *btn_edit = make_icon_btn(row, LV_SYMBOL_EDIT,
                                           lv_palette_main(LV_PALETTE_BLUE_GREY));
        lv_obj_align(btn_edit, LV_ALIGN_RIGHT_MID, -44, 0);
        lv_obj_add_event_cb(btn_edit, [](lv_event_t *ev) {
            session_meta_t *meta = (session_meta_t *)lv_event_get_user_data(ev);
            s_selected_id = meta->id;
            strlcpy(s_selected_note, meta->note, sizeof(s_selected_note));
            open_note_modal();
        }, LV_EVENT_CLICKED, &copy_buf[i]);

        // ── Delete — LV_SYMBOL_TRASH ──────────────────────────────────────────
        lv_obj_t *btn_trash = make_icon_btn(row, LV_SYMBOL_TRASH,
                                            lv_palette_main(LV_PALETTE_RED));
        lv_obj_align(btn_trash, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_add_event_cb(btn_trash, [](lv_event_t *ev) {
            session_meta_t *meta = (session_meta_t *)lv_event_get_user_data(ev);
            s_confirm_id = meta->id;
            lv_obj_clear_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, &copy_buf[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Note modal
// ─────────────────────────────────────────────────────────────────────────────

static void open_note_modal(void)
{
    lv_textarea_set_text(s_ta_note_edit, s_selected_note);
    lv_keyboard_set_textarea(s_kb_note_edit, s_ta_note_edit);
    lv_obj_clear_flag(s_note_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(s_ta_note_edit, 0, LV_ANIM_OFF);
}

static void close_note_modal(void)
{
    const char *text = lv_textarea_get_text(s_ta_note_edit);
    strlcpy(s_selected_note, text, sizeof(s_selected_note));
    session_save_note(s_selected_id, s_selected_note);
    lv_obj_add_flag(s_note_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb_note_edit, NULL);
    // Refresh list so updated note is available on next tap
    rebuild_list();
}

static void on_note_done(lv_event_t *e)      { (void)e; close_note_modal(); }
static void on_note_kb_ready(lv_event_t *e)  { (void)e; close_note_modal(); }

static void on_note_ta_focused(lv_event_t *e)
{
    (void)e;
    lv_keyboard_set_textarea(s_kb_note_edit, s_ta_note_edit);
    lv_obj_clear_flag(s_kb_note_edit, LV_OBJ_FLAG_HIDDEN);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SESSION_MENU);
}

static void on_confirm_delete(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    if (s_confirm_id == 0) return;
    esp_err_t err = session_delete(s_confirm_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Deleted session %" PRIu32, s_confirm_id);
    } else {
        ESP_LOGE(TAG, "Delete session %" PRIu32 " failed: %s",
                 s_confirm_id, esp_err_to_name(err));
    }
    s_confirm_id = 0;
    rebuild_list();
}

static void on_confirm_cancel(lv_event_t *e)
{
    (void)e;
    s_confirm_id = 0;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_note_overlay,  LV_OBJ_FLAG_HIDDEN);
    rebuild_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_list_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header ────────────────────────────────────────────────────────────────
    s_hdr = lv_obj_create(s_scr);
    lv_obj_set_size(s_hdr, 480, 40);
    lv_obj_set_pos(s_hdr, 0, 0);
    lv_obj_clear_flag(s_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_hdr, 0, LV_PART_MAIN);

    lv_obj_t *btn_back = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_back, 60, 32);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 10);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, i18n_t(STR_BTN_BACK));
    lv_obj_set_style_text_font(s_lbl_back, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_LIST));
    lv_obj_set_style_text_font(s_lbl_title, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Empty label ───────────────────────────────────────────────────────────
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, i18n_t(STR_HISTORY_EMPTY));
    lv_obj_set_style_text_font(s_lbl_empty, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_empty, 0, 150);
    lv_obj_set_width(s_lbl_empty, 480);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    // ── Scrollable list ───────────────────────────────────────────────────────
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, 480, 280);
    lv_obj_set_pos(s_list, 0, 40);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 4, LV_PART_MAIN);

    // ── Delete confirmation popup ─────────────────────────────────────────────
    //
    // Centred card (280×140) over a semi-transparent full-screen dimmer.
    s_confirm_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_confirm_panel, 480, 320);
    lv_obj_set_pos(s_confirm_panel, 0, 0);
    lv_obj_clear_flag(s_confirm_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_confirm_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_confirm_panel, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_confirm_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_confirm_panel, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);

    // Centred white card
    lv_obj_t *card = lv_obj_create(s_confirm_panel);
    lv_obj_set_size(card, 300, 150);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 16, LV_PART_MAIN);

    s_lbl_confirm_msg = lv_label_create(card);
    lv_label_set_text(s_lbl_confirm_msg, i18n_t(STR_SESSION_CONFIRM_DELETE));
    lv_obj_set_style_text_font(s_lbl_confirm_msg, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_width(s_lbl_confirm_msg, 268);
    lv_label_set_long_mode(s_lbl_confirm_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_lbl_confirm_msg, LV_ALIGN_TOP_LEFT, 0, 0);

    // Cancel button (left)
    lv_obj_t *btn_cancel = lv_btn_create(card);
    lv_obj_set_size(btn_cancel, 120, 44);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_cancel, 8);
    lv_obj_add_event_cb(btn_cancel, on_confirm_cancel, LV_EVENT_CLICKED, NULL);
    s_lbl_confirm_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(s_lbl_confirm_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_font(s_lbl_confirm_cancel, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_confirm_cancel, lv_color_black(), LV_PART_MAIN);
    lv_obj_center(s_lbl_confirm_cancel);

    // Delete button (right)
    lv_obj_t *btn_ok = lv_btn_create(card);
    lv_obj_set_size(btn_ok, 120, 44);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_ok, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_ok, 8);
    lv_obj_add_event_cb(btn_ok, on_confirm_delete, LV_EVENT_CLICKED, NULL);
    s_lbl_confirm_ok = lv_label_create(btn_ok);
    lv_label_set_text(s_lbl_confirm_ok, i18n_t(STR_SESSION_DELETE));
    lv_obj_set_style_text_font(s_lbl_confirm_ok, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_confirm_ok, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(s_lbl_confirm_ok);

    // ── Note edit modal overlay ────────────────────────────────────────────────
    //
    // Layout (480×320):
    //   y=  0..50  header: "Session note" label + Done button
    //   y= 54..119  textarea (65px)
    //   y=120..320  keyboard (200px)
    s_note_overlay = lv_obj_create(s_scr);
    lv_obj_set_size(s_note_overlay, 480, 320);
    lv_obj_set_pos(s_note_overlay, 0, 0);
    lv_obj_clear_flag(s_note_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_note_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_note_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_note_overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_note_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl_note_title = lv_label_create(s_note_overlay);
    lv_label_set_text(lbl_note_title, i18n_t(STR_SESSION_NOTE));
    lv_obj_set_style_text_font(lbl_note_title, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(lbl_note_title, 8, 14);

    lv_obj_t *btn_note_done = lv_btn_create(s_note_overlay);
    lv_obj_set_size(btn_note_done, 80, 32);
    lv_obj_set_pos(btn_note_done, 392, 8);
    lv_obj_set_style_radius(btn_note_done, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_note_done, on_note_done, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_note_done = lv_label_create(btn_note_done);
    lv_label_set_text(lbl_note_done, i18n_t(STR_BTN_OK));
    lv_obj_set_style_text_font(lbl_note_done, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(lbl_note_done);

    s_ta_note_edit = lv_textarea_create(s_note_overlay);
    lv_obj_set_size(s_ta_note_edit, 464, 65);
    lv_obj_set_pos(s_ta_note_edit, 8, 54);
    lv_textarea_set_one_line(s_ta_note_edit, false);
    lv_textarea_set_max_length(s_ta_note_edit, SESSION_NOTE_MAX - 1);
    lv_textarea_set_placeholder_text(s_ta_note_edit, i18n_t(STR_SESSION_NOTE_PLACEHOLDER));
    lv_obj_set_scrollbar_mode(s_ta_note_edit, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_font(s_ta_note_edit, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ta_note_edit, on_note_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_note_edit, on_note_ta_focused, LV_EVENT_CLICKED, NULL);

    s_kb_note_edit = lv_keyboard_create(s_note_overlay);
    lv_obj_set_size(s_kb_note_edit, 480, 200);
    lv_obj_align(s_kb_note_edit, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb_note_edit, &pilocows_font_22, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb_note_edit, on_note_kb_ready, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb_note_edit, on_note_kb_ready, LV_EVENT_CANCEL, NULL);

    ESP_LOGI(TAG, "Session list screen created");
}

// ─────────────────────────────────────────────────────────────────────────────
// Load / refresh
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_list_load(void)
{
    lv_scr_load(s_scr);
    // rebuild_list() and overlay resets run via on_screen_loaded
}

void screen_session_list_refresh_language(void)
{
    lv_label_set_text(s_lbl_title,          i18n_t(STR_SESSION_LIST));
    lv_label_set_text(s_lbl_back,           i18n_t(STR_BTN_BACK));
    lv_label_set_text(s_lbl_confirm_msg,    i18n_t(STR_SESSION_CONFIRM_DELETE));
    lv_label_set_text(s_lbl_confirm_ok,     i18n_t(STR_SESSION_DELETE));
    lv_label_set_text(s_lbl_confirm_cancel, i18n_t(STR_BTN_CANCEL));
    // Rebuild list rows so type strings pick up the new language
    rebuild_list();
}
