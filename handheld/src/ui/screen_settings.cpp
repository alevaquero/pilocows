#include "screen_settings.h"
#include "screen_scan.h"
#include "screen_wifi.h"
#include "screen_session_menu.h"
#include "screen_session_new.h"
#include "screen_session_list.h"
#include "screen_vaccine_settings.h"
#include "screen_test_settings.h"
#include "screen_ble_sync.h"
#include "ui_manager.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "peripherals/buzzer.h"
#include "peripherals/vibrator.h"
#include "display/display.h"
#include "rtc/rtc.h"
#include "lvgl.h"
#include <time.h>
#include <sys/time.h>
#include "fonts.h"

static lv_obj_t *s_screen           = NULL;
static lv_obj_t *s_lbl_title        = NULL;
static lv_obj_t *s_lbl_back         = NULL;
static lv_obj_t *s_lbl_language     = NULL;
static lv_obj_t *s_lbl_buzzer       = NULL;
static lv_obj_t *s_lbl_vibrator     = NULL;
static lv_obj_t *s_lbl_brightness   = NULL;
static lv_obj_t *s_lbl_datetime     = NULL;
static lv_obj_t *s_lbl_datetime_btn = NULL;  // "Set Time" button label inside the row
static lv_obj_t *s_lbl_wifi         = NULL;
static lv_obj_t *s_lbl_wifi_btn     = NULL;  // "Configure" button label inside the row
static lv_obj_t *s_lbl_vaccines     = NULL;
static lv_obj_t *s_lbl_vaccines_btn = NULL;  // "Configure" button label inside the row
static lv_obj_t *s_lbl_tests        = NULL;
static lv_obj_t *s_lbl_tests_btn    = NULL;  // "Configure" button label inside the row
static lv_obj_t *s_lbl_sync         = NULL;
static lv_obj_t *s_lbl_sync_btn     = NULL;  // "Sync to PC" button label inside the row

// Spinbox refs kept alive while the datetime modal is open; NULL otherwise.
static lv_obj_t *s_sb_year  = NULL;
static lv_obj_t *s_sb_month = NULL;
static lv_obj_t *s_sb_day   = NULL;
static lv_obj_t *s_sb_hour  = NULL;
static lv_obj_t *s_sb_min   = NULL;

// ---------------------------------------------------------------------------
// Language refresh
// ---------------------------------------------------------------------------
static void refresh_language(void)
{
    lv_label_set_text(s_lbl_title,        i18n_t(STR_SETTINGS_TITLE));
    lv_label_set_text(s_lbl_back,         i18n_t(STR_BTN_BACK));
    lv_label_set_text(s_lbl_language,     i18n_t(STR_SETTINGS_LANGUAGE));
    lv_label_set_text(s_lbl_buzzer,       i18n_t(STR_SETTINGS_BUZZER));
    lv_label_set_text(s_lbl_vibrator,     i18n_t(STR_SETTINGS_VIBRATOR));
    lv_label_set_text(s_lbl_brightness,   i18n_t(STR_SETTINGS_BRIGHTNESS));
    lv_label_set_text(s_lbl_datetime,     i18n_t(STR_SETTINGS_DATETIME));
    lv_label_set_text(s_lbl_datetime_btn, i18n_t(STR_SETTINGS_SET_TIME));
    lv_label_set_text(s_lbl_wifi,         i18n_t(STR_SETTINGS_WIFI));
    lv_label_set_text(s_lbl_wifi_btn,     i18n_t(STR_WIFI_CONFIGURE));
    lv_label_set_text(s_lbl_vaccines,     i18n_t(STR_SETTINGS_VACCINES));
    lv_label_set_text(s_lbl_vaccines_btn, i18n_t(STR_WIFI_CONFIGURE));
    lv_label_set_text(s_lbl_tests,        i18n_t(STR_SETTINGS_TESTS));
    lv_label_set_text(s_lbl_tests_btn,    i18n_t(STR_WIFI_CONFIGURE));
    lv_label_set_text(s_lbl_sync,         i18n_t(STR_SETTINGS_SYNC));
    lv_label_set_text(s_lbl_sync_btn,     i18n_t(STR_SETTINGS_SYNC));
}

// ---------------------------------------------------------------------------
// Event callbacks
// ---------------------------------------------------------------------------
static void on_back(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SESSION_MENU);
}

static void on_language(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    i18n_set_language(en ? LANG_EN : LANG_ES);
    refresh_language();
    screen_scan_refresh_language();
    screen_wifi_refresh_language();
    screen_session_menu_refresh_language();
    screen_session_new_refresh_language();
    screen_session_list_refresh_language();
    screen_vaccine_settings_refresh_language();
    screen_ble_sync_refresh_language();
}

static void on_buzzer(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    buzzer_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_vibrator(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    vibrator_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_brightness(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    display_set_brightness((uint8_t)val);
}

// ---------------------------------------------------------------------------
// Date & Time modal
// ---------------------------------------------------------------------------

static void close_modal(lv_obj_t *modal)
{
    lv_obj_del_async(modal);
    s_sb_year = s_sb_month = s_sb_day = s_sb_hour = s_sb_min = NULL;
}

// Create one spinbox column: label on top, ▲ button, spinbox, ▼ button.
// col_w must be wide enough for digit_count digits at montserrat_20
// (4-digit fields need ~90px, 2-digit fields need ~68px).
static lv_obj_t *make_spinbox_col(lv_obj_t *parent, const char *label,
                                   int min_val, int max_val, int init_val,
                                   int digit_count, int col_w, int x, int y)
{
    const int btn_h  = 44;
    const int sb_h   = 52;   // taller so digits fit without feeling cramped
    const int lbl_h  = 18;

    // Field label — sized to column width and center-aligned
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &pilocows_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(lbl, col_w, lbl_h);
    lv_obj_set_pos(lbl, x, y);

    // ▲ button
    lv_obj_t *btn_up = lv_btn_create(parent);
    lv_obj_set_size(btn_up, col_w, btn_h);
    lv_obj_set_pos(btn_up, x, y + lbl_h + 2);
    lv_obj_t *lbl_up = lv_label_create(btn_up);
    lv_label_set_text(lbl_up, LV_SYMBOL_UP);
    lv_obj_center(lbl_up);

    // Spinbox — scrolling and animations disabled so digits stay still.
    // After each value change, scroll is reset to (0,0) with no animation
    // so the cursor never causes the content to shift.
    lv_obj_t *sb = lv_spinbox_create(parent);
    lv_spinbox_set_range(sb, min_val, max_val);
    lv_spinbox_set_value(sb, init_val);
    lv_spinbox_set_digit_format(sb, digit_count, 0);
    lv_obj_set_size(sb, col_w, sb_h);
    lv_obj_set_pos(sb, x, y + lbl_h + 2 + btn_h + 2);
    lv_obj_clear_flag(sb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_anim_time(sb, 0, 0);
    lv_obj_add_event_cb(sb, [](lv_event_t *e) {
        lv_obj_scroll_to(lv_event_get_target(e), 0, 0, LV_ANIM_OFF);
    }, LV_EVENT_VALUE_CHANGED, NULL);

    // ▼ button
    lv_obj_t *btn_dn = lv_btn_create(parent);
    lv_obj_set_size(btn_dn, col_w, btn_h);
    lv_obj_set_pos(btn_dn, x, y + lbl_h + 2 + btn_h + 2 + sb_h + 2);
    lv_obj_t *lbl_dn = lv_label_create(btn_dn);
    lv_label_set_text(lbl_dn, LV_SYMBOL_DOWN);
    lv_obj_center(lbl_dn);

    lv_obj_add_event_cb(btn_up, [](lv_event_t *e) {
        lv_spinbox_increment((lv_obj_t *)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, sb);

    lv_obj_add_event_cb(btn_dn, [](lv_event_t *e) {
        lv_spinbox_decrement((lv_obj_t *)lv_event_get_user_data(e));
    }, LV_EVENT_CLICKED, sb);

    return sb;
}

static void on_datetime_set(lv_event_t *e)
{
    struct tm tm = {};
    tm.tm_year = lv_spinbox_get_value(s_sb_year) - 1900;
    tm.tm_mon  = lv_spinbox_get_value(s_sb_month) - 1;
    tm.tm_mday = lv_spinbox_get_value(s_sb_day);
    tm.tm_hour = lv_spinbox_get_value(s_sb_hour);
    tm.tm_min  = lv_spinbox_get_value(s_sb_min);
    tm.tm_sec  = 0;
    time_t t = mktime(&tm);
    struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    rtc_set_time(t);

    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(e);
    close_modal(modal);
}

static void on_datetime_cancel(lv_event_t *e)
{
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(e);
    close_modal(modal);
}

static void on_datetime_edit(lv_event_t *e)
{
    // Dim overlay covering the whole screen
    lv_obj_t *overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(overlay, 480, 320);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Dialog card — padding reduced to 4px so content has maximum room.
    // Layout (y relative to card interior, content area = 270 - 2*4 = 262px):
    //   y=4:   title (~25px tall)
    //   y=32:  spinbox columns: lbl(18)+▲(44)+sb(52)+▼(44) + 2px gaps = 164px → bottom 196
    //   y=216: action buttons (h=40) → bottom 256  (256 < 262 ✓)
    lv_obj_t *card = lv_obj_create(overlay);
    lv_obj_set_size(card, 440, 270);
    lv_obj_center(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, i18n_t(STR_SETTINGS_SET_TIME));
    lv_obj_set_style_text_font(title, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Pre-fill with current system time
    time_t now = time(NULL);
    struct tm *cur = localtime(&now);

    // Five columns centered inside card inner width (440 - 2*4 pad = 432px):
    //   w4=90, w2=68, gaps=4, date/time gap=12 → total span=390px → left_off=21px
    const int top  = 32;
    const int w4   = 90;
    const int w2   = 68;
    const int g    = 12;
    const int col_gap = 4;
    const int inner_w = 432;
    const int span = w4 + col_gap + w2 + col_gap + w2 + col_gap + g + w2 + col_gap + w2;
    const int xo   = (inner_w - span) / 2;   // ≈ 21px left offset
    const int x0   = xo,            x1 = x0+w4+col_gap,
              x2   = x1+w2+col_gap, x3 = x2+w2+col_gap+g,
              x4   = x3+w2+col_gap;
    s_sb_year  = make_spinbox_col(card, i18n_t(STR_DATETIME_YEAR),  2020, 2099, cur->tm_year + 1900, 4, w4, x0, top);
    s_sb_month = make_spinbox_col(card, i18n_t(STR_DATETIME_MONTH),    1,   12, cur->tm_mon + 1,     2, w2, x1, top);
    s_sb_day   = make_spinbox_col(card, i18n_t(STR_DATETIME_DAY),      1,   31, cur->tm_mday,        2, w2, x2, top);
    s_sb_hour  = make_spinbox_col(card, i18n_t(STR_DATETIME_HOUR),     0,   23, cur->tm_hour,        2, w2, x3, top);
    s_sb_min   = make_spinbox_col(card, i18n_t(STR_DATETIME_MIN),      0,   59, cur->tm_min,         2, w2, x4, top);

    // Action buttons — aligned to the edges of the column group
    const int btn_w = 130;
    lv_obj_t *btn_cancel = lv_btn_create(card);
    lv_obj_set_size(btn_cancel, btn_w, 40);
    lv_obj_set_pos(btn_cancel, xo, 216);
    lv_obj_add_event_cb(btn_cancel, on_datetime_cancel, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_center(lbl_cancel);

    lv_obj_t *btn_set = lv_btn_create(card);
    lv_obj_set_size(btn_set, btn_w, 40);
    lv_obj_set_pos(btn_set, xo + span - btn_w, 216);
    lv_obj_add_event_cb(btn_set, on_datetime_set, LV_EVENT_CLICKED, overlay);
    lv_obj_t *lbl_set = lv_label_create(btn_set);
    lv_label_set_text(lbl_set, i18n_t(STR_SETTINGS_SET_TIME));
    lv_obj_center(lbl_set);
}

// ---------------------------------------------------------------------------
// Screen creation
// ---------------------------------------------------------------------------
void screen_settings_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_text_font(s_screen, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    // ── Fixed header (44 px tall) ────────────────────────────────────────────
    lv_obj_t *hdr = lv_obj_create(s_screen);
    lv_obj_set_size(hdr, 480, 44);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_border_color(hdr, lv_color_hex(0xDDE1E7), LV_PART_MAIN);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(hdr);
    lv_obj_set_size(btn_back, 100, 34);   // 34px leaves 5px gap from header top and bottom border
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    // ext_click_area=20 fills the remaining 5px gaps to header edges and extends left into the bezel.
    lv_obj_set_ext_click_area(btn_back, 20);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, i18n_t(STR_BTN_BACK));
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_TITLE));
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Scrollable row panel (below header) ──────────────────────────────────
    lv_obj_t *panel = lv_obj_create(s_screen);
    lv_obj_set_size(panel, 480, 276);   // 320 - 44 header
    lv_obj_set_pos(panel, 0, 44);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 0, LV_PART_MAIN);
    // Scrollable by default; disable horizontal scroll
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ACTIVE);

    // Each row: label on the left, control on the right.
    // row_h = 50px gives comfortable touch targets with visual breathing room.
    // A thin 1-px separator is drawn between rows.
    int row_y = 8;
    const int row_h  = 50;
    const int sep_h  = 1;
    const int btn_x  = 318;   // right-aligned controls start here
    const int btn_w  = 148;
    const int btn_h  = 34;

    // ext_click_area values expand each control's hit zone to fill the full row height.
    // Derived from control position within the row:
    //   switch (h≈26, offset 13): 13px fills gap to row top; symmetric → 13
    //   button (h=34, offset  8):  8px fills gap to row top; symmetric → 8
    //   slider (h=20, offset 16): 15px fills gap to row top; symmetric → 15
    const int sw_ext  = 13;
    const int btn_ext =  8;
    const int sl_ext  = 15;

    // Helper lambda: add a horizontal separator
    auto add_sep = [&]() {
        lv_obj_t *sep = lv_obj_create(panel);
        lv_obj_set_size(sep, 440, sep_h);
        lv_obj_set_pos(sep, 20, row_y);
        lv_obj_set_style_radius(sep, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(sep, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(sep, lv_color_hex(0xDDE1E7), LV_PART_MAIN);
        row_y += sep_h;
    };

    // ── Language ─────────────────────────────────────────────────────────────
    {
        s_lbl_language = lv_label_create(panel);
        lv_label_set_text(s_lbl_language, i18n_t(STR_SETTINGS_LANGUAGE));
        lv_obj_set_pos(s_lbl_language, 20, row_y + 15);

        lv_obj_t *lbl_ind = lv_label_create(panel);
        lv_label_set_text(lbl_ind, "ES | EN");
        lv_obj_set_pos(lbl_ind, 270, row_y + 17);

        lv_obj_t *sw = lv_switch_create(panel);
        lv_obj_set_pos(sw, 400, row_y + 13);
        lv_obj_set_ext_click_area(sw, sw_ext);
        if (i18n_get_language() == LANG_EN) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_language, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    add_sep();

    // ── Buzzer ───────────────────────────────────────────────────────────────
    {
        s_lbl_buzzer = lv_label_create(panel);
        lv_label_set_text(s_lbl_buzzer, i18n_t(STR_SETTINGS_BUZZER));
        lv_obj_set_pos(s_lbl_buzzer, 20, row_y + 15);

        lv_obj_t *sw = lv_switch_create(panel);
        lv_obj_set_pos(sw, 400, row_y + 13);
        lv_obj_set_ext_click_area(sw, sw_ext);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_buzzer, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    add_sep();

    // ── Vibrator ─────────────────────────────────────────────────────────────
    {
        s_lbl_vibrator = lv_label_create(panel);
        lv_label_set_text(s_lbl_vibrator, i18n_t(STR_SETTINGS_VIBRATOR));
        lv_obj_set_pos(s_lbl_vibrator, 20, row_y + 15);

        lv_obj_t *sw = lv_switch_create(panel);
        lv_obj_set_pos(sw, 400, row_y + 13);
        lv_obj_set_ext_click_area(sw, sw_ext);
        lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_vibrator, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    add_sep();

    // ── Brightness ───────────────────────────────────────────────────────────
    {
        s_lbl_brightness = lv_label_create(panel);
        lv_label_set_text(s_lbl_brightness, i18n_t(STR_SETTINGS_BRIGHTNESS));
        lv_obj_set_pos(s_lbl_brightness, 20, row_y + 15);

        lv_obj_t *slider = lv_slider_create(panel);
        lv_obj_set_size(slider, btn_w, 20);
        lv_obj_set_pos(slider, btn_x, row_y + 16);
        lv_obj_set_ext_click_area(slider, sl_ext);
        lv_slider_set_range(slider, 20, 100);
        lv_slider_set_value(slider, 80, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    add_sep();

    // ── Date & Time ──────────────────────────────────────────────────────────
    {
        s_lbl_datetime = lv_label_create(panel);
        lv_label_set_text(s_lbl_datetime, i18n_t(STR_SETTINGS_DATETIME));
        lv_obj_set_pos(s_lbl_datetime, 20, row_y + 15);

        lv_obj_t *btn_edit = lv_btn_create(panel);
        lv_obj_set_size(btn_edit, btn_w, btn_h);
        lv_obj_set_pos(btn_edit, btn_x, row_y + 8);
        lv_obj_set_ext_click_area(btn_edit, btn_ext);
        lv_obj_add_event_cb(btn_edit, on_datetime_edit, LV_EVENT_CLICKED, NULL);
        s_lbl_datetime_btn = lv_label_create(btn_edit);
        lv_label_set_text(s_lbl_datetime_btn, i18n_t(STR_SETTINGS_SET_TIME));
        lv_obj_center(s_lbl_datetime_btn);
        row_y += row_h;
    }

    add_sep();

    // ── WiFi ─────────────────────────────────────────────────────────────────
    {
        s_lbl_wifi = lv_label_create(panel);
        lv_label_set_text(s_lbl_wifi, i18n_t(STR_SETTINGS_WIFI));
        lv_obj_set_pos(s_lbl_wifi, 20, row_y + 15);

        lv_obj_t *btn_wifi = lv_btn_create(panel);
        lv_obj_set_size(btn_wifi, btn_w, btn_h);
        lv_obj_set_pos(btn_wifi, btn_x, row_y + 8);
        lv_obj_set_ext_click_area(btn_wifi, btn_ext);
        lv_obj_add_event_cb(btn_wifi, [](lv_event_t *) {
            ui_manager_show(SCREEN_WIFI);
        }, LV_EVENT_CLICKED, NULL);
        s_lbl_wifi_btn = lv_label_create(btn_wifi);
        lv_label_set_text(s_lbl_wifi_btn, i18n_t(STR_WIFI_CONFIGURE));
        lv_obj_center(s_lbl_wifi_btn);
        row_y += row_h;
    }

    add_sep();

    // ── Vaccines ─────────────────────────────────────────────────────────────
    {
        s_lbl_vaccines = lv_label_create(panel);
        lv_label_set_text(s_lbl_vaccines, i18n_t(STR_SETTINGS_VACCINES));
        lv_obj_set_pos(s_lbl_vaccines, 20, row_y + 15);

        lv_obj_t *btn_vax = lv_btn_create(panel);
        lv_obj_set_size(btn_vax, btn_w, btn_h);
        lv_obj_set_pos(btn_vax, btn_x, row_y + 8);
        lv_obj_set_ext_click_area(btn_vax, btn_ext);
        lv_obj_add_event_cb(btn_vax, [](lv_event_t *) {
            ui_manager_show(SCREEN_VACCINE_SETTINGS);
        }, LV_EVENT_CLICKED, NULL);
        s_lbl_vaccines_btn = lv_label_create(btn_vax);
        lv_label_set_text(s_lbl_vaccines_btn, i18n_t(STR_WIFI_CONFIGURE));
        lv_obj_center(s_lbl_vaccines_btn);
        row_y += row_h;
    }

    add_sep();

    // ── Tests ─────────────────────────────────────────────────────────────────
    {
        s_lbl_tests = lv_label_create(panel);
        lv_label_set_text(s_lbl_tests, i18n_t(STR_SETTINGS_TESTS));
        lv_obj_set_pos(s_lbl_tests, 20, row_y + 15);

        lv_obj_t *btn_tests = lv_btn_create(panel);
        lv_obj_set_size(btn_tests, btn_w, btn_h);
        lv_obj_set_pos(btn_tests, btn_x, row_y + 8);
        lv_obj_set_ext_click_area(btn_tests, btn_ext);
        lv_obj_add_event_cb(btn_tests, [](lv_event_t *) {
            ui_manager_show(SCREEN_TEST_SETTINGS);
        }, LV_EVENT_CLICKED, NULL);
        s_lbl_tests_btn = lv_label_create(btn_tests);
        lv_label_set_text(s_lbl_tests_btn, i18n_t(STR_WIFI_CONFIGURE));
        lv_obj_center(s_lbl_tests_btn);
        row_y += row_h;
    }

    add_sep();

    // ── Sync to PC ───────────────────────────────────────────────────────────
    {
        s_lbl_sync = lv_label_create(panel);
        lv_label_set_text(s_lbl_sync, i18n_t(STR_SETTINGS_SYNC));
        lv_obj_set_pos(s_lbl_sync, 20, row_y + 15);

        lv_obj_t *btn_sync = lv_btn_create(panel);
        lv_obj_set_size(btn_sync, btn_w, btn_h);
        lv_obj_set_pos(btn_sync, btn_x, row_y + 8);
        lv_obj_set_ext_click_area(btn_sync, btn_ext);
        lv_obj_add_event_cb(btn_sync, [](lv_event_t *) {
            screen_ble_sync_show_modal();
        }, LV_EVENT_CLICKED, NULL);
        s_lbl_sync_btn = lv_label_create(btn_sync);
        lv_label_set_text(s_lbl_sync_btn, i18n_t(STR_SETTINGS_SYNC));
        lv_obj_center(s_lbl_sync_btn);
        row_y += row_h;
    }

    // ── Version (inside panel, scrolls with content) ─────────────────────────
    {
        lv_obj_t *lbl = lv_label_create(panel);
        lv_label_set_text(lbl, "Pilocows v0.1.0");
        lv_obj_set_style_text_font(lbl, &pilocows_font_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
        lv_obj_set_pos(lbl, 0, row_y + 10);
        lv_obj_set_width(lbl, 480);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    }
}

void screen_settings_load(void)
{
    lv_scr_load(s_screen);
}
