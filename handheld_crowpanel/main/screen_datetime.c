#include "screen_datetime.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "i18n.h"
#include "strings_en.h"
#include "soft_rtc.h"
#include "lvgl.h"
#include <time.h>
#include <sys/time.h>

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_lbl_back = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_cancel = NULL;
static lv_obj_t *s_lbl_set = NULL;

// Spinbox refs, re-seeded with the current time each time the screen loads.
static lv_obj_t *s_sb_year = NULL;
static lv_obj_t *s_sb_month = NULL;
static lv_obj_t *s_sb_day = NULL;
static lv_obj_t *s_sb_hour = NULL;
static lv_obj_t *s_sb_min = NULL;

static void on_back(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SETTINGS); }

static void on_set(lv_event_t *e) {
    (void)e;
    struct tm tm = {0};
    tm.tm_year = lv_spinbox_get_value(s_sb_year) - 1900;
    tm.tm_mon = lv_spinbox_get_value(s_sb_month) - 1;
    tm.tm_mday = lv_spinbox_get_value(s_sb_day);
    tm.tm_hour = lv_spinbox_get_value(s_sb_hour);
    tm.tm_min = lv_spinbox_get_value(s_sb_min);
    tm.tm_sec = 0;
    time_t t = mktime(&tm);
    soft_rtc_set_time(t);
    ui_manager_show(SCREEN_SETTINGS);
}

static void on_spinbox_value_changed(lv_event_t *e) {
    lv_obj_scroll_to(lv_event_get_target(e), 0, 0, LV_ANIM_OFF);
}

static void on_spinbox_up(lv_event_t *e) {
    lv_spinbox_increment((lv_obj_t *)lv_event_get_user_data(e));
}

static void on_spinbox_down(lv_event_t *e) {
    lv_spinbox_decrement((lv_obj_t *)lv_event_get_user_data(e));
}

// Create one spinbox column: label on top, up button, spinbox, down button.
static lv_obj_t *make_spinbox_col(lv_obj_t *parent, const char *label,
                                   int min_val, int max_val, int init_val,
                                   int digit_count, int col_w, int x, int y) {
    const int btn_h = 66;
    const int sb_h = 78;
    const int lbl_h = 27;

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(lbl, col_w, lbl_h);
    lv_obj_set_pos(lbl, x, y);

    lv_obj_t *btn_up = lv_btn_create(parent);
    lv_obj_set_size(btn_up, col_w, btn_h);
    lv_obj_set_pos(btn_up, x, y + lbl_h + 3);
    lv_obj_t *lbl_up = lv_label_create(btn_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_UP);
    lv_obj_center(lbl_up);

    lv_obj_t *sb = lv_spinbox_create(parent);
    lv_spinbox_set_range(sb, min_val, max_val);
    lv_spinbox_set_value(sb, init_val);
    lv_spinbox_set_digit_format(sb, digit_count, 0);
    lv_obj_set_size(sb, col_w, sb_h);
    lv_obj_set_pos(sb, x, y + lbl_h + 3 + btn_h + 3);
    lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_anim_time(sb, 0, 0);
    lv_obj_set_style_text_font(sb, &lv_font_app_36, LV_PART_MAIN);
    lv_obj_set_style_text_align(sb, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    // Center vertically too: montserrat_36's line height is 40px, so split
    // the leftover box height evenly above/below instead of the default
    // top-anchored textarea padding.
    lv_obj_set_style_pad_ver(sb, (sb_h - 40) / 2, LV_PART_MAIN);
    lv_obj_add_event_cb(sb, on_spinbox_value_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_dn = lv_btn_create(parent);
    lv_obj_set_size(btn_dn, col_w, btn_h);
    lv_obj_set_pos(btn_dn, x, y + lbl_h + 3 + btn_h + 3 + sb_h + 3);
    lv_obj_t *lbl_dn = lv_label_create(btn_dn);
    lv_label_set_text(lbl_dn, LV_SYMBOL_DOWN);
    lv_obj_center(lbl_dn);

    lv_obj_add_event_cb(btn_up, on_spinbox_up, LV_EVENT_CLICKED, sb);
    lv_obj_add_event_cb(btn_dn, on_spinbox_down, LV_EVENT_CLICKED, sb);

    return sb;
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    time_t now = time(NULL);
    struct tm *cur = localtime(&now);
    lv_spinbox_set_value(s_sb_year, cur->tm_year + 1900);
    lv_spinbox_set_value(s_sb_month, cur->tm_mon + 1);
    lv_spinbox_set_value(s_sb_day, cur->tm_mday);
    lv_spinbox_set_value(s_sb_hour, cur->tm_hour);
    lv_spinbox_set_value(s_sb_min, cur->tm_min);
}

void screen_datetime_create(void) {
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
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(s_lbl_back, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_DATETIME));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Spinbox rows: Year/Month/Day, then Hour/Minute ───────────────────────
    time_t now = time(NULL);
    struct tm *cur = localtime(&now);

    const int header_h = 80;
    const int col_h = 246; // label + up-btn + spinbox + down-btn stack height
    const int row_gap = 20;
    const int btn_w = 180, btn_h = 60, btn_gap = 20;
    const int margin_bottom = 30;

    // Cancel/Set Time sit near the bottom of the screen; the two spinbox
    // rows are vertically centered in the remaining space above them.
    const int btn_y = 800 - margin_bottom - btn_h;
    const int block_h = col_h * 2 + row_gap;
    const int top1 = header_h + (btn_y - row_gap - header_h - block_h) / 2;
    const int top2 = top1 + col_h + row_gap;
    const int w4 = 150;
    const int w2 = 113;
    const int col_gap = 7;
    const int inner_w = 432; // 480 - 2*24 margin
    const int margin_x = 24;

    // Row 1: Year | Month | Day
    const int span1 = w4 + col_gap + w2 + col_gap + w2;
    const int x0 = margin_x + (inner_w - span1) / 2;
    const int x1 = x0 + w4 + col_gap, x2 = x1 + w2 + col_gap;
    s_sb_year = make_spinbox_col(s_scr, i18n_t(STR_DATETIME_YEAR), 2020, 2099, cur->tm_year + 1900, 4, w4, x0, top1);
    s_sb_month = make_spinbox_col(s_scr, i18n_t(STR_DATETIME_MONTH), 1, 12, cur->tm_mon + 1, 2, w2, x1, top1);
    s_sb_day = make_spinbox_col(s_scr, i18n_t(STR_DATETIME_DAY), 1, 31, cur->tm_mday, 2, w2, x2, top1);

    // Row 2: Hour | Minute
    const int span2 = w2 + col_gap + w2;
    const int xh = margin_x + (inner_w - span2) / 2, xm = xh + w2 + col_gap;
    s_sb_hour = make_spinbox_col(s_scr, i18n_t(STR_DATETIME_HOUR), 0, 23, cur->tm_hour, 2, w2, xh, top2);
    s_sb_min = make_spinbox_col(s_scr, i18n_t(STR_DATETIME_MIN), 0, 59, cur->tm_min, 2, w2, xm, top2);

    // ── Cancel (red, left) / Set Time (default, right) — centered pair ───────
    const int pair_w = btn_w * 2 + btn_gap;
    const int pair_x = (480 - pair_w) / 2;

    lv_obj_t *btn_cancel = lv_btn_create(s_scr);
    lv_obj_set_size(btn_cancel, btn_w, btn_h);
    lv_obj_set_pos(btn_cancel, pair_x, btn_y);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_cancel, on_back, LV_EVENT_CLICKED, NULL);
    s_lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(s_lbl_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_color(s_lbl_cancel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_cancel, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(s_lbl_cancel);

    lv_obj_t *btn_set = lv_btn_create(s_scr);
    lv_obj_set_size(btn_set, btn_w, btn_h);
    lv_obj_set_pos(btn_set, pair_x + btn_w + btn_gap, btn_y);
    lv_obj_set_style_radius(btn_set, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_set, on_set, LV_EVENT_CLICKED, NULL);
    s_lbl_set = lv_label_create(btn_set);
    lv_label_set_text(s_lbl_set, i18n_t(STR_SETTINGS_SET_TIME));
    lv_obj_set_style_text_font(s_lbl_set, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(s_lbl_set);
}

void screen_datetime_load(void) {
    lv_scr_load(s_scr);
}

void screen_datetime_refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_DATETIME));
    lv_label_set_text(s_lbl_cancel, i18n_t(STR_BTN_CANCEL));
    lv_label_set_text(s_lbl_set, i18n_t(STR_SETTINGS_SET_TIME));
}
