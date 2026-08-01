#include "ui_text_entry.h"
#include "app_fonts.h"
#include "ui_keyboard.h"
#include "i18n.h"
#include "strings_en.h"
#include <string.h>

#define TEXT_ENTRY_BUF_MAX 256

// Layout constants (480x800 portrait canvas).
#define LABEL_Y      20
#define LABEL_H      40
#define FIELD_Y      70
#define FIELD_H_1LN  66
#define FIELD_H_ML   220
#define EYE_W        83
#define BTN_ROW_Y    420
#define BTN_H        60
#define BTN_W        140
#define BTN_GAP      20
#define MARGIN_X     20

static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_ta = NULL;
static lv_obj_t *s_btn_eye = NULL;
static lv_obj_t *s_lbl_eye = NULL;
static bool s_pass_visible = false;

static ui_text_entry_confirm_cb_t s_on_confirm = NULL;
static ui_text_entry_cancel_cb_t s_on_cancel = NULL;
static void *s_user_data = NULL;

static void close_entry(void) {
    if (s_overlay) {
        lv_obj_del_async(s_overlay);
    }
    s_overlay = NULL;
    s_ta = NULL;
    s_btn_eye = NULL;
    s_lbl_eye = NULL;
    s_on_confirm = NULL;
    s_on_cancel = NULL;
    s_user_data = NULL;
}

static void do_confirm(void) {
    ui_text_entry_confirm_cb_t cb = s_on_confirm;
    void *ud = s_user_data;
    char buf[TEXT_ENTRY_BUF_MAX];
    const char *text = lv_textarea_get_text(s_ta);
    strncpy(buf, text ? text : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    close_entry();
    if (cb) cb(buf, ud);
}

static void do_cancel(void) {
    ui_text_entry_cancel_cb_t cb = s_on_cancel;
    void *ud = s_user_data;
    close_entry();
    if (cb) cb(ud);
}

static void on_ok_btn(lv_event_t *e) { (void)e; do_confirm(); }
static void on_cancel_btn(lv_event_t *e) { (void)e; do_cancel(); }

static void on_kb_event(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_READY) {
        do_confirm();
    } else {
        do_cancel();
    }
}

static void on_eye_toggle(lv_event_t *e) {
    (void)e;
    s_pass_visible = !s_pass_visible;
    lv_textarea_set_password_mode(s_ta, !s_pass_visible);
    lv_label_set_text(s_lbl_eye, s_pass_visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

void ui_text_entry_show(const ui_text_entry_cfg_t *cfg) {
    if (!cfg) return;
    if (s_overlay) close_entry(); // safety net: don't stack modals

    s_on_confirm = cfg->on_confirm;
    s_on_cancel = cfg->on_cancel;
    s_user_data = cfg->user_data;
    s_pass_visible = false;

    s_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_overlay, 480, 800);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);

    lv_obj_t *lbl_title = lv_label_create(s_overlay);
    lv_label_set_text(lbl_title, cfg->label ? cfg->label : "");
    lv_obj_set_style_text_font(lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(lbl_title, 480, LABEL_H);
    lv_obj_set_pos(lbl_title, 0, LABEL_Y);

    int field_h = cfg->multiline ? FIELD_H_ML : FIELD_H_1LN;
    int field_w = cfg->password ? (480 - 2 * MARGIN_X - EYE_W - 10) : (480 - 2 * MARGIN_X);

    s_ta = lv_textarea_create(s_overlay);
    lv_obj_set_size(s_ta, field_w, field_h);
    // Center the field (or, for password mode, the field+eye-button cluster as
    // a whole) horizontally rather than hand-computing an x from the margin,
    // so it can't drift off-center regardless of field_w/EYE_W tweaks.
    lv_obj_align(s_ta, LV_ALIGN_TOP_MID, cfg->password ? -(EYE_W + 10) / 2 : 0, FIELD_Y);
    lv_textarea_set_one_line(s_ta, !cfg->multiline);
    lv_textarea_set_password_mode(s_ta, cfg->password);
    if (cfg->max_length > 0) {
        lv_textarea_set_max_length(s_ta, cfg->max_length);
    }
    if (cfg->placeholder) {
        lv_textarea_set_placeholder_text(s_ta, cfg->placeholder);
    }
    if (cfg->initial_text) {
        lv_textarea_set_text(s_ta, cfg->initial_text);
    }
    lv_obj_set_scrollbar_mode(s_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_font(s_ta, &lv_font_app_28, LV_PART_MAIN);

    if (cfg->password) {
        s_btn_eye = lv_btn_create(s_overlay);
        lv_obj_set_size(s_btn_eye, EYE_W, field_h);
        lv_obj_align_to(s_btn_eye, s_ta, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
        lv_obj_add_event_cb(s_btn_eye, on_eye_toggle, LV_EVENT_CLICKED, NULL);
        s_lbl_eye = lv_label_create(s_btn_eye);
        lv_label_set_text(s_lbl_eye, LV_SYMBOL_EYE_OPEN);
        lv_obj_center(s_lbl_eye);
    }

    // Cancel (red, left of the pair) / OK (theme accent, right of the pair) -
    // a compact pair centered as a block above the keyboard, not pinned to
    // the screen edges.
    int pair_w = BTN_W * 2 + BTN_GAP;
    int pair_x = (480 - pair_w) / 2;

    lv_obj_t *btn_cancel = lv_btn_create(s_overlay);
    lv_obj_set_size(btn_cancel, BTN_W, BTN_H);
    lv_obj_set_pos(btn_cancel, pair_x, BTN_ROW_Y);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_cancel, on_cancel_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_color(lbl_cancel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_cancel, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_cancel);

    lv_obj_t *btn_ok = lv_btn_create(s_overlay);
    lv_obj_set_size(btn_ok, BTN_W, BTN_H);
    lv_obj_set_pos(btn_ok, pair_x + BTN_W + BTN_GAP, BTN_ROW_Y);
    lv_obj_set_style_radius(btn_ok, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_ok, on_ok_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_ok = lv_label_create(btn_ok);
    lv_label_set_text(lbl_ok, i18n_t(STR_BTN_OK));
    lv_obj_set_style_text_font(lbl_ok, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_ok);

    lv_obj_t *kb = ui_keyboard_create(s_overlay);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, on_kb_event, LV_EVENT_CANCEL, NULL);

    lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
}
