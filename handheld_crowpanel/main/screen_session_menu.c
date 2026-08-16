#include "screen_session_menu.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "session_storage.h"
#include "soft_rtc.h"
#include "bsp_stc8h1kxx.h"
#include "ui_icons.h"
#include "i18n.h"
#include "strings_en.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <inttypes.h>
#include <time.h>

static const char *TAG = "scr_sess_menu";

// ── Static label refs (for refresh_language) ─────────────────────────────────
static lv_obj_t *s_lbl_no_session;
static lv_obj_t *s_lbl_btn_resume;
static lv_obj_t *s_lbl_btn_new;
static lv_obj_t *s_lbl_btn_list;
static lv_obj_t *s_lbl_btn_settings;

// ── Status header (date/time + battery) ───────────────────────────────────────
static lv_obj_t *s_lbl_clock;
static lv_obj_t *s_lbl_battery;
static lv_timer_t *s_clock_timer;
static lv_timer_t *s_battery_timer;

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

static void clock_tick_cb(lv_timer_t *t) {
    (void)t;
    struct tm tm_info;
    soft_rtc_get_local_tm(&tm_info);
    char buf[24];
    strftime(buf, sizeof(buf), "%d %b %H:%M", &tm_info);
    lv_label_set_text(s_lbl_clock, buf);
}

// Battery level -> icon glyph. Thresholds are ours (the STC8 only reports a
// raw 0-100 percentage); no documented breakpoints exist to match instead.
static const char *battery_icon_for_level(uint8_t level) {
    if (level >= 85) return LV_SYMBOL_BATTERY_FULL;
    if (level >= 60) return LV_SYMBOL_BATTERY_3;
    if (level >= 35) return LV_SYMBOL_BATTERY_2;
    if (level >= 12) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

// The STC8's charge-state enum (IDLE/CHARGING/FULLY_CHARGED/NO_CHARGE/ERROR)
// has no "battery absent" value — it's a simple ADC read on the battery
// voltage divider, so a disconnected battery leaves that pin floating
// rather than reading a real cell voltage. A floating ADC input typically
// settles near a rail or reference voltage, which lands outside any real
// single-cell Li-ion/LiPo's actual operating range (~3.0-4.2V) and gets
// mistaken for "full" by the gauge. Filtering on bat_voltage plausibility
// is a heuristic, not a documented API - the exact bounds may need
// retuning once we've seen real readings both with and without a battery
// attached (see the ESP_LOGI below).
#define BAT_VOLTAGE_MIN_MV 2500
#define BAT_VOLTAGE_MAX_MV 4300

// Base rate is cheap enough on its own (the I2C read is a handful of
// single-byte register reads, sub-millisecond, on a bus that's already
// active for touch/RTC) that plug/unplug is already caught within 4s. The
// burst window on top of that just makes the following few readings (e.g.
// percentage ticking up right after a plug-in) feel continuously live
// instead of stepping every 4s, without polling fast all the time.
#define BATTERY_POLL_BASE_MS 4000
#define BATTERY_POLL_BURST_MS 1000
#define BATTERY_POLL_BURST_WINDOW_US (10 * 1000 * 1000) // stay fast for 10s after a detected change

static bool s_last_charging = false;
static bool s_have_last_charging = false;
static int64_t s_burst_until_us = 0;

static void battery_tick_cb(lv_timer_t *t) {
    int64_t now = esp_timer_get_time();

    Battery_info_t info;
    if (stc8_battery_info_get(&info) != ESP_OK) {
        lv_obj_set_style_text_color(s_lbl_battery, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
        lv_label_set_text(s_lbl_battery, LV_SYMBOL_BATTERY_EMPTY " --");
        lv_timer_set_period(t, now < s_burst_until_us ? BATTERY_POLL_BURST_MS : BATTERY_POLL_BASE_MS);
        return;
    }

    ESP_LOGI(TAG, "battery: adc=%" PRIu32 "mV bat=%" PRIu32 "mV level=%u%% state=%u",
             info.adc_voltage, info.bat_voltage, info.bat_level, info.bat_state);

    if (info.bat_voltage < BAT_VOLTAGE_MIN_MV || info.bat_voltage > BAT_VOLTAGE_MAX_MV) {
        lv_obj_set_style_text_color(s_lbl_battery, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
        lv_label_set_text(s_lbl_battery, LV_SYMBOL_BATTERY_EMPTY " N/A");
        lv_timer_set_period(t, now < s_burst_until_us ? BATTERY_POLL_BURST_MS : BATTERY_POLL_BASE_MS);
        return;
    }

    bool charging = (info.bat_state == BAT_CHARGE_CHARGING || info.bat_state == BAT_CHARGE_FULLY_CHARGED);
    const char *icon = battery_icon_for_level(info.bat_level);

    if (s_have_last_charging && charging != s_last_charging) {
        ESP_LOGI(TAG, "battery: charge state changed (charging=%d) - burst polling for %dms",
                 charging, BATTERY_POLL_BURST_WINDOW_US / 1000);
        s_burst_until_us = now + BATTERY_POLL_BURST_WINDOW_US;
    }
    s_last_charging = charging;
    s_have_last_charging = true;
    lv_timer_set_period(t, now < s_burst_until_us ? BATTERY_POLL_BURST_MS : BATTERY_POLL_BASE_MS);

    lv_color_t color;
    if (charging) {
        color = lv_palette_main(LV_PALETTE_GREEN);
    } else if (info.bat_level < 15) {
        color = lv_palette_main(LV_PALETTE_RED);
    } else {
        color = lv_color_white();
    }
    lv_obj_set_style_text_color(s_lbl_battery, color, LV_PART_MAIN);

    if (charging) {
        lv_label_set_text_fmt(s_lbl_battery, "%s %s %d%%", LV_SYMBOL_CHARGE, icon, info.bat_level);
    } else {
        lv_label_set_text_fmt(s_lbl_battery, "%s %d%%", icon, info.bat_level);
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

    // ── Status header (fixed, 50px — date/time left, battery right) ─────────
    lv_obj_t *hdr = lv_obj_create(s_scr);
    lv_obj_set_size(hdr, 480, 50);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x2c3e50), 0);

    s_lbl_clock = lv_label_create(hdr);
    lv_label_set_text(s_lbl_clock, "-- --- --:--");
    lv_obj_set_style_text_font(s_lbl_clock, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_clock, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_clock, LV_ALIGN_LEFT_MID, 14, 0);

    s_lbl_battery = lv_label_create(hdr);
    lv_label_set_text(s_lbl_battery, LV_SYMBOL_BATTERY_EMPTY " --");
    lv_obj_set_style_text_font(s_lbl_battery, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_battery, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_battery, LV_ALIGN_RIGHT_MID, -14, 0);

    // ── Body (below header) — same rows as before the header existed, just
    // reparented from s_scr to this panel with the 3 inter-row gaps tightened
    // from 60px to 43px (was tuned to fill exactly 800px with zero slack; the
    // header's 50px has to come from somewhere, and shrinking gaps keeps
    // every row's own size — and touch target — unchanged):
    //   y= 12  Active session card       (454x190 — name/type/count, one per line)
    //   y=169  "No active session" label (centered, shown when no card)
    //   y=245  Resume row                (454x105, hidden when no active session)
    //   y=393  New + List rows           (454x105 each, stacked, always visible)
    //   y=658  Settings button           (454x78, always visible, flush to bottom)
    lv_obj_t *panel = lv_obj_create(s_scr);
    lv_obj_set_size(panel, 480, 750);
    lv_obj_set_pos(panel, 0, 50);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN);

    s_card = lv_obj_create(panel);
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
    s_row_resume = lv_obj_create(panel);
    lv_obj_set_size(s_row_resume, 454, 105);
    lv_obj_set_pos(s_row_resume, 13, 245);
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
    s_lbl_no_session = lv_label_create(panel);
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SESSION_NONE));
    lv_obj_set_style_text_font(s_lbl_no_session, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_no_session, 0, 169);
    lv_obj_set_width(s_lbl_no_session, 480);
    lv_obj_set_style_text_align(s_lbl_no_session, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    // ── New + List rows (always visible) ──────────────────────────────────
    // Stacked (was side-by-side) since 375w+385w doesn't fit 480px width.
    lv_obj_t *row_always = lv_obj_create(panel);
    lv_obj_set_size(row_always, 454, 222);
    lv_obj_set_pos(row_always, 13, 393);
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
    lv_obj_t *btn_settings = lv_btn_create(panel);
    lv_obj_set_size(btn_settings, 454, 78);
    lv_obj_set_pos(btn_settings, 13, 658);
    lv_obj_set_style_radius(btn_settings, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_settings, lv_color_hex(0x607D8B), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_settings, 20);
    lv_obj_add_event_cb(btn_settings, on_settings, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_settings = lv_label_create(btn_settings);
    lv_label_set_text_fmt(s_lbl_btn_settings, "%s  %s", UI_SYMBOL_SETTINGS, i18n_t(STR_SETTINGS_TITLE));
    lv_obj_set_style_text_color(s_lbl_btn_settings, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_btn_settings, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_settings);

    // ── Initial state (hidden until load) ───────────────────────────────────
    lv_obj_add_flag(s_card, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_row_resume, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_no_session, LV_OBJ_FLAG_HIDDEN);

    clock_tick_cb(NULL);
    s_clock_timer = lv_timer_create(clock_tick_cb, 1000, NULL);
    s_battery_timer = lv_timer_create(battery_tick_cb, BATTERY_POLL_BASE_MS, NULL);
    battery_tick_cb(s_battery_timer); // immediate first read, reusing the real timer so it can adjust its own period

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
    lv_label_set_text_fmt(s_lbl_btn_settings, "%s  %s", UI_SYMBOL_SETTINGS, i18n_t(STR_SETTINGS_TITLE));
    refresh_state();
}
