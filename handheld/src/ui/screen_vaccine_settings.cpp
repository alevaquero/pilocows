#include "screen_vaccine_settings.h"
#include "ui_manager.h"
#include "lvgl.h"
#include "../i18n/i18n.h"
#include "../i18n/strings_en.h"
#include "../storage/session_storage.h"
#include "esp_log.h"
#include <string.h>
#include "fonts.h"

static const char *TAG = "scr_vax";

// ── Static label refs ─────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_back;
static lv_obj_t *s_lbl_empty;
static lv_obj_t *s_lbl_btn_add;

// ── List ──────────────────────────────────────────────────────────────────────
static lv_obj_t *s_list;

// ── Add overlay ───────────────────────────────────────────────────────────────
static lv_obj_t *s_add_panel;
static lv_obj_t *s_ta_name;
static lv_obj_t *s_kb;
static lv_obj_t *s_lbl_add_title;

// ── Delete confirm overlay ────────────────────────────────────────────────────
static lv_obj_t *s_del_panel;
static lv_obj_t *s_lbl_del_msg;
static lv_obj_t *s_lbl_del_confirm;
static lv_obj_t *s_lbl_del_cancel;
static uint8_t   s_del_pending_id = 0;

static lv_obj_t *s_hdr  = NULL;
static lv_obj_t *s_scr  = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// List rebuild
// ─────────────────────────────────────────────────────────────────────────────

static void rebuild_list(void)
{
    lv_obj_clean(s_list);

    vaccine_cfg_t vlist[VACCINE_LIST_MAX];
    int count = vaccine_list(vlist, VACCINE_LIST_MAX);

    if (count == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < count; i++) {
        uint8_t vid = vlist[i].id;

        lv_obj_t *row = lv_list_add_btn(s_list, NULL, "");
        lv_obj_set_height(row, 48);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        // Disable the default click behaviour — we handle delete separately
        lv_obj_remove_event_cb(row, NULL);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, vlist[i].name);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, 340);
        lv_obj_set_style_text_font(lbl, &pilocows_font_18, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);

        lv_obj_t *btn_del = lv_btn_create(row);
        lv_obj_set_size(btn_del, 80, 34);
        lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, -4, 0);
        lv_obj_set_style_bg_color(btn_del, lv_color_hex(0xC0392B), LV_PART_MAIN);
        lv_obj_set_style_radius(btn_del, 4, LV_PART_MAIN);

        lv_obj_t *lbl_del = lv_label_create(btn_del);
        lv_label_set_text(lbl_del, i18n_t(STR_VACCINE_DELETE));
        lv_obj_set_style_text_color(lbl_del, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(lbl_del, &pilocows_font_18, LV_PART_MAIN);
        lv_obj_center(lbl_del);

        // Pass vaccine id via user_data
        static uint8_t id_buf[VACCINE_LIST_MAX];
        id_buf[i] = vid;
        lv_obj_add_event_cb(btn_del, [](lv_event_t *ev) {
            uint8_t id = *(uint8_t *)lv_event_get_user_data(ev);
            s_del_pending_id = id;
            lv_obj_clear_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, &id_buf[i]);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Add overlay helpers
// ─────────────────────────────────────────────────────────────────────────────

static void show_add(void)
{
    lv_textarea_set_text(s_ta_name, "");
    lv_obj_add_flag   (s_hdr,       LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag   (s_list,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag   (s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag (s_add_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag (s_kb,        LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, s_ta_name);
}

static void hide_add(void)
{
    lv_obj_clear_flag (s_hdr,       LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag (s_list,      LV_OBJ_FLAG_HIDDEN);
    // s_lbl_empty visibility is managed by rebuild_list
    lv_obj_add_flag   (s_add_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag   (s_kb,        LV_OBJ_FLAG_HIDDEN);
    rebuild_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SETTINGS);
}

static void on_add_btn(lv_event_t *e)
{
    (void)e;
    show_add();
}

static void on_add_confirm(lv_event_t *e)
{
    (void)e;
    const char *name = lv_textarea_get_text(s_ta_name);
    if (name && name[0]) {
        uint8_t new_id = 0;
        esp_err_t err = vaccine_add(name, &new_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "vaccine_add failed: %s", esp_err_to_name(err));
        }
    }
    hide_add();
    rebuild_list();
}

static void on_ta_focused(lv_event_t *e)
{
    (void)e;
    // keyboard is already shown by show_add(); nothing extra needed
}

static void on_kb_ready(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        on_add_confirm(NULL);   // ✓ key — save
    } else {
        hide_add();             // × key — discard
    }
}

static void on_del_confirm(lv_event_t *e)
{
    (void)e;
    if (s_del_pending_id != 0) {
        vaccine_delete(s_del_pending_id);
        s_del_pending_id = 0;
    }
    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
    rebuild_list();
}

static void on_del_cancel(lv_event_t *e)
{
    (void)e;
    s_del_pending_id = 0;
    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;
    hide_add();
    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
    rebuild_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────

void screen_vaccine_settings_create(void)
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
    lv_label_set_text(s_lbl_title, i18n_t(STR_VACCINES_TITLE));
    lv_obj_set_style_text_font(s_lbl_title, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Add button (top-right of header) ─────────────────────────────────────
    lv_obj_t *btn_add = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_add, 130, 32);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_border_width(btn_add, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_add, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_add, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_add, 10);
    lv_obj_add_event_cb(btn_add, on_add_btn, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_add = lv_label_create(btn_add);
    lv_label_set_text(s_lbl_btn_add, i18n_t(STR_VACCINE_ADD));
    lv_obj_set_style_text_font(s_lbl_btn_add, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_add);

    // ── Empty label ───────────────────────────────────────────────────────────
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, i18n_t(STR_VACCINE_NONE));
    lv_obj_set_style_text_font(s_lbl_empty, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_empty, 0, 150);
    lv_obj_set_width(s_lbl_empty, 480);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    // ── Vaccine list ──────────────────────────────────────────────────────────
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, 480, 280);
    lv_obj_set_pos(s_list, 0, 40);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 4, LV_PART_MAIN);

    // ── Add overlay — pinned to top so keyboard (y=120–320) never covers it ──
    // Layout: header(40) | add_panel(78) | ... | keyboard(200) at bottom
    s_add_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_add_panel, 460, 78);
    lv_obj_set_pos(s_add_panel, 10, 40);
    lv_obj_clear_flag(s_add_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_add_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_add_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_add_panel, 8, LV_PART_MAIN);

    s_lbl_add_title = lv_label_create(s_add_panel);
    lv_label_set_text(s_lbl_add_title, i18n_t(STR_VACCINE_NAME));
    lv_obj_set_style_text_font(s_lbl_add_title, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_align(s_lbl_add_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_ta_name = lv_textarea_create(s_add_panel);
    lv_obj_set_size(s_ta_name, 440, 50);
    lv_obj_align(s_ta_name, LV_ALIGN_TOP_LEFT, 0, 24);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_max_length(s_ta_name, VACCINE_NAME_MAX - 1);
    lv_obj_set_scrollbar_mode(s_ta_name, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_name, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ta_name, on_ta_focused, LV_EVENT_FOCUSED, NULL);
    // Confirm/cancel via keyboard ✓ / × keys (on_kb_ready handles both)

    lv_obj_add_flag(s_add_panel, LV_OBJ_FLAG_HIDDEN);

    // ── Keyboard (shared, floats over screen bottom) ──────────────────────────
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, 480, 200);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb, &pilocows_font_22, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    // ── Delete confirm overlay ────────────────────────────────────────────────
    s_del_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_del_panel, 360, 120);
    lv_obj_align(s_del_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_del_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(s_del_panel, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_del_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_del_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_del_panel, 12, LV_PART_MAIN);

    s_lbl_del_msg = lv_label_create(s_del_panel);
    lv_label_set_text(s_lbl_del_msg, i18n_t(STR_VACCINE_CONFIRM_DELETE));
    lv_obj_set_style_text_font(s_lbl_del_msg, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_width(s_lbl_del_msg, 330);
    lv_obj_set_style_text_align(s_lbl_del_msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_lbl_del_msg, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *btn_del_ok = lv_btn_create(s_del_panel);
    lv_obj_set_size(btn_del_ok, 140, 36);
    lv_obj_align(btn_del_ok, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_del_ok, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_del_ok, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_del_ok, 10);
    lv_obj_add_event_cb(btn_del_ok, on_del_confirm, LV_EVENT_CLICKED, NULL);
    s_lbl_del_confirm = lv_label_create(btn_del_ok);
    lv_label_set_text(s_lbl_del_confirm, i18n_t(STR_SESSION_DELETE));
    lv_obj_set_style_text_color(s_lbl_del_confirm, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_del_confirm, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_del_confirm);

    lv_obj_t *btn_del_no = lv_btn_create(s_del_panel);
    lv_obj_set_size(btn_del_no, 140, 36);
    lv_obj_align(btn_del_no, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_border_width(btn_del_no, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_del_no, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_del_no, 10);
    lv_obj_add_event_cb(btn_del_no, on_del_cancel, LV_EVENT_CLICKED, NULL);
    s_lbl_del_cancel = lv_label_create(btn_del_no);
    lv_label_set_text(s_lbl_del_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_font(s_lbl_del_cancel, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_del_cancel);

    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Vaccine settings screen created");
}

// ─────────────────────────────────────────────────────────────────────────────
// Load / refresh
// ─────────────────────────────────────────────────────────────────────────────

void screen_vaccine_settings_load(void)
{
    lv_scr_load(s_scr);
}

void screen_vaccine_settings_refresh_language(void)
{
    lv_label_set_text(s_lbl_title,        i18n_t(STR_VACCINES_TITLE));
    lv_label_set_text(s_lbl_back,         i18n_t(STR_BTN_BACK));
    lv_label_set_text(s_lbl_btn_add,      i18n_t(STR_VACCINE_ADD));
    lv_label_set_text(s_lbl_empty,        i18n_t(STR_VACCINE_NONE));
    lv_label_set_text(s_lbl_add_title,    i18n_t(STR_VACCINE_NAME));
    lv_label_set_text(s_lbl_del_msg,      i18n_t(STR_VACCINE_CONFIRM_DELETE));
    lv_label_set_text(s_lbl_del_confirm,  i18n_t(STR_SESSION_DELETE));
    lv_label_set_text(s_lbl_del_cancel,   i18n_t(STR_BTN_CANCEL));
    rebuild_list();
}
