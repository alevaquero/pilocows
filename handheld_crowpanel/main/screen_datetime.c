#include "screen_datetime.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "i18n.h"
#include "strings_en.h"
#include "soft_rtc.h"
#include "ui_icons.h"
#include "lvgl.h"
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_cancel = NULL;
static lv_obj_t *s_lbl_set = NULL;

// Spinbox refs, re-seeded with the current (local) time each time the
// screen loads.
static lv_obj_t *s_sb_year = NULL;
static lv_obj_t *s_sb_month = NULL;
static lv_obj_t *s_sb_day = NULL;
static lv_obj_t *s_sb_hour = NULL;
static lv_obj_t *s_sb_min = NULL;

// ── Timezone control ─────────────────────────────────────────────────────────
// Staged like the spinboxes above — only actually persisted (via
// soft_rtc_set_tz_offset_min) when "Set Time" is pressed, so Cancel truly
// discards every change on this screen, not just the date/time fields.
#define TZ_STEP_MIN (60)
#define TZ_MIN_MIN  (-720) // UTC-12:00
#define TZ_MAX_MIN  (840)  // UTC+14:00

static int16_t s_staged_tz_offset_min = 0;
static lv_obj_t *s_lbl_tz_title = NULL;
static lv_obj_t *s_lbl_tz_value = NULL;

static void format_tz_offset(int16_t minutes, char *buf, size_t buf_len) {
    if (minutes == 0) {
        snprintf(buf, buf_len, "UTC");
        return;
    }
    char sign = minutes < 0 ? '-' : '+';
    int abs_min = minutes < 0 ? -minutes : minutes;
    snprintf(buf, buf_len, "UTC%c%02d:%02d", sign, abs_min / 60, abs_min % 60);
}

static void update_tz_value_label(void) {
    char buf[16];
    format_tz_offset(s_staged_tz_offset_min, buf, sizeof(buf));
    lv_label_set_text(s_lbl_tz_value, buf);
}

static void on_tz_minus(lv_event_t *e) {
    (void)e;
    if (s_staged_tz_offset_min - TZ_STEP_MIN < TZ_MIN_MIN) return;
    s_staged_tz_offset_min -= TZ_STEP_MIN;
    update_tz_value_label();
}

static void on_tz_plus(lv_event_t *e) {
    (void)e;
    if (s_staged_tz_offset_min + TZ_STEP_MIN > TZ_MAX_MIN) return;
    s_staged_tz_offset_min += TZ_STEP_MIN;
    update_tz_value_label();
}

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
    // Order matters: persist the offset first so soft_rtc_set_local_tm()
    // (which reads it back from NVS) converts using the new value, not a
    // stale one.
    soft_rtc_set_tz_offset_min(s_staged_tz_offset_min);
    soft_rtc_set_local_tm(&tm);
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
    ui_icon_create(btn_up, UI_SYMBOL_UP, lv_color_white(), &lv_font_app_30);

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
    ui_icon_create(btn_dn, UI_SYMBOL_DOWN, lv_color_white(), &lv_font_app_30);

    lv_obj_add_event_cb(btn_up, on_spinbox_up, LV_EVENT_CLICKED, sb);
    lv_obj_add_event_cb(btn_dn, on_spinbox_down, LV_EVENT_CLICKED, sb);

    return sb;
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    s_staged_tz_offset_min = soft_rtc_get_tz_offset_min();
    update_tz_value_label();

    struct tm cur;
    soft_rtc_get_local_tm(&cur);
    lv_spinbox_set_value(s_sb_year, cur.tm_year + 1900);
    lv_spinbox_set_value(s_sb_month, cur.tm_mon + 1);
    lv_spinbox_set_value(s_sb_day, cur.tm_mday);
    lv_spinbox_set_value(s_sb_hour, cur.tm_hour);
    lv_spinbox_set_value(s_sb_min, cur.tm_min);
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
    ui_icon_create(btn_back, UI_SYMBOL_BACK, lv_color_white(), &lv_font_app_30);

    s_lbl_title = lv_label_create(hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_DATETIME));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Timezone row, then spinbox rows: Year/Month/Day, then Hour/Minute ───
    struct tm cur_tm;
    soft_rtc_get_local_tm(&cur_tm);
    struct tm *cur = &cur_tm;

    const int header_h = 80;
    const int tz_h = 64;    // Timezone row: title label + minus/value/plus controls
    const int col_h = 246;  // label + up-btn + spinbox + down-btn stack height
    const int row_gap = 20;
    const int btn_w = 180, btn_h = 60, btn_gap = 20;
    const int margin_bottom = 30;
    const int inner_w = 432; // 480 - 2*24 margin
    const int margin_x = 24;

    // Cancel/Set Time sit near the bottom of the screen; the timezone row
    // and the two spinbox rows are vertically centered as one block in the
    // remaining space above them.
    const int btn_y = 800 - margin_bottom - btn_h;
    const int block_h = tz_h + row_gap + col_h * 2 + row_gap;
    const int top_tz = header_h + (btn_y - row_gap - header_h - block_h) / 2;
    const int top1 = top_tz + tz_h + row_gap;
    const int top2 = top1 + col_h + row_gap;
    const int w4 = 150;
    const int w2 = 113;
    const int col_gap = 7;

    // ── Timezone: title label + minus/value/plus row ─────────────────────────
    s_lbl_tz_title = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_tz_title, i18n_t(STR_DATETIME_TIMEZONE));
    lv_obj_set_style_text_font(s_lbl_tz_title, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_tz_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_lbl_tz_title, inner_w, 20);
    lv_obj_set_pos(s_lbl_tz_title, margin_x, top_tz);

    const int tz_ctrl_w = 44, tz_val_w = 200, tz_ctrl_gap = 8;
    const int tz_span = tz_ctrl_w + tz_ctrl_gap + tz_val_w + tz_ctrl_gap + tz_ctrl_w;
    const int tz_x = margin_x + (inner_w - tz_span) / 2;
    const int tz_ctrl_y = top_tz + 24;

    lv_obj_t *btn_tz_minus = lv_btn_create(s_scr);
    lv_obj_set_size(btn_tz_minus, tz_ctrl_w, tz_ctrl_w);
    lv_obj_set_pos(btn_tz_minus, tz_x, tz_ctrl_y);
    lv_obj_add_event_cb(btn_tz_minus, on_tz_minus, LV_EVENT_CLICKED, NULL);
    ui_icon_create(btn_tz_minus, UI_SYMBOL_DOWN, lv_color_white(), &lv_font_app_28);

    s_lbl_tz_value = lv_label_create(s_scr);
    lv_label_set_long_mode(s_lbl_tz_value, LV_LABEL_LONG_CLIP); // never wrap to a 2nd (invisible) line
    lv_obj_set_style_text_font(s_lbl_tz_value, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_tz_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_lbl_tz_value, tz_val_w, tz_ctrl_w);
    lv_obj_set_pos(s_lbl_tz_value, tz_x + tz_ctrl_w + tz_ctrl_gap, tz_ctrl_y);
    lv_obj_set_style_pad_ver(s_lbl_tz_value, (tz_ctrl_w - 32) / 2, LV_PART_MAIN);

    lv_obj_t *btn_tz_plus = lv_btn_create(s_scr);
    lv_obj_set_size(btn_tz_plus, tz_ctrl_w, tz_ctrl_w);
    lv_obj_set_pos(btn_tz_plus, tz_x + tz_ctrl_w + tz_ctrl_gap + tz_val_w + tz_ctrl_gap, tz_ctrl_y);
    lv_obj_add_event_cb(btn_tz_plus, on_tz_plus, LV_EVENT_CLICKED, NULL);
    ui_icon_create(btn_tz_plus, UI_SYMBOL_UP, lv_color_white(), &lv_font_app_28);

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
    lv_label_set_text(s_lbl_tz_title, i18n_t(STR_DATETIME_TIMEZONE));
}
