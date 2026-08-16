#include "screen_wifi.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "ui_text_entry.h"
#include "i18n.h"
#include "strings_en.h"
#include "wifi_manager.h"
#include "ui_icons.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

// ── Layout (480x800 portrait) ─────────────────────────────────────────────────
#define MAX_APS 20
#define OPTS_BUF (MAX_APS * 34)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_header = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_lbl_net = NULL;
static lv_obj_t *s_dd_network = NULL;
static lv_obj_t *s_lbl_pass = NULL;
static lv_obj_t *s_ta_pass = NULL;
static lv_obj_t *s_btn_conn = NULL;
static lv_obj_t *s_lbl_conn = NULL;
static lv_obj_t *s_lbl_error = NULL;

static bool s_connecting = false;
static int s_connecting_ticks = 0;

static volatile bool s_auth_error = false;
static int s_error_ticks = 0;

static wifi_ap_t s_scan_aps[MAX_APS];
static uint16_t s_scan_count = 0;
static volatile bool s_scan_ready = false;
static lv_timer_t *s_scan_timer = NULL;

static void set_connecting(bool on) {
    s_connecting = on;
    if (on) {
        s_connecting_ticks = 15;
        lv_obj_add_state(s_btn_conn, LV_STATE_DISABLED);
        lv_label_set_text(s_lbl_conn, i18n_t(STR_WIFI_CONNECTING));
    } else {
        lv_obj_clear_state(s_btn_conn, LV_STATE_DISABLED);
        lv_label_set_text(s_lbl_conn, i18n_t(STR_WIFI_CONNECT));
    }
}

static void on_scan_done(const wifi_ap_t *aps, uint16_t count) {
    s_scan_count = count < MAX_APS ? count : MAX_APS;
    for (uint16_t i = 0; i < s_scan_count; i++) s_scan_aps[i] = aps[i];
    s_scan_ready = true;
}

static void update_dropdown(void) {
    static char opts[OPTS_BUF];
    if (s_scan_count == 0) {
        lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_NO_NETWORKS));
    } else {
        opts[0] = '\0';
        for (uint16_t i = 0; i < s_scan_count; i++) {
            if (i > 0) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
            strncat(opts, s_scan_aps[i].ssid, sizeof(opts) - strlen(opts) - 1);
        }
        lv_dropdown_set_options(s_dd_network, opts);
    }
    lv_obj_clear_state(s_dd_network, LV_STATE_DISABLED);
}

static void scan_poll_cb(lv_timer_t *t) {
    if (!s_scan_ready) return;
    s_scan_ready = false;
    s_scan_timer = NULL;
    update_dropdown();
    lv_timer_del(t);
}

static void start_scan(void) {
    lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_SEARCHING));
    lv_obj_add_state(s_dd_network, LV_STATE_DISABLED);
    s_scan_ready = false;
    wifi_scan_start(on_scan_done);
    if (!s_scan_timer) s_scan_timer = lv_timer_create(scan_poll_cb, 200, NULL);
}

static void on_auth_error(void) { s_auth_error = true; }

static void status_tick_cb(lv_timer_t *t) {
    (void)t;

    if (s_auth_error) {
        s_auth_error = false;
        set_connecting(false);
        lv_label_set_text(s_lbl_error, i18n_t(STR_WIFI_WRONG_PASS));
        lv_obj_clear_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);
        s_error_ticks = 4;
    }
    if (s_error_ticks > 0 && --s_error_ticks == 0) {
        lv_obj_add_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_connecting) {
        if (wifi_is_connected()) {
            set_connecting(false);
        } else if (--s_connecting_ticks <= 0) {
            set_connecting(false);
        }
    }

    char buf[64];
    if (wifi_is_connected()) {
        lv_obj_set_style_text_color(s_lbl_status, lv_color_make(80, 220, 80), LV_PART_MAIN);
        snprintf(buf, sizeof(buf), "IP: %s", wifi_get_ip_str());
    } else {
        lv_obj_set_style_text_color(s_lbl_status, lv_color_make(180, 180, 180), LV_PART_MAIN);
        snprintf(buf, sizeof(buf), "%s", i18n_t(STR_WIFI_DISCONNECTED));
    }
    lv_label_set_text(s_lbl_status, buf);
}

static void on_back(lv_event_t *e) {
    (void)e;
    ui_manager_show(SCREEN_SETTINGS);
}

static void on_rescan(lv_event_t *e) { (void)e; start_scan(); }

static void on_connect(lv_event_t *e) {
    (void)e;
    char ssid[33] = {0};
    lv_dropdown_get_selected_str(s_dd_network, ssid, sizeof(ssid));
    if (ssid[0] == '\0' ||
        strcmp(ssid, i18n_t(STR_WIFI_SEARCHING)) == 0 ||
        strcmp(ssid, i18n_t(STR_WIFI_NO_NETWORKS)) == 0) return;

    set_connecting(true);
    wifi_set_credentials(ssid, lv_textarea_get_text(s_ta_pass));
}

// Password is edited via the shared ui_text_entry modal (tap to open, masked
// with an eye-toggle there); this field just displays the current value.
static void on_pass_confirm(const char *text, void *user_data) {
    (void)user_data;
    lv_textarea_set_text(s_ta_pass, text);
}

static void on_pass_clicked(lv_event_t *e) {
    (void)e;
    ui_text_entry_cfg_t cfg = {
        .label = i18n_t(STR_WIFI_PASSWORD),
        .initial_text = lv_textarea_get_text(s_ta_pass),
        .placeholder = NULL,
        .multiline = false,
        .password = true,
        .max_length = 0,
        .on_confirm = on_pass_confirm,
        .on_cancel = NULL,
        .user_data = NULL,
    };
    ui_text_entry_show(&cfg);
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    lv_textarea_set_text(s_ta_pass, "");
    lv_obj_add_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);
    s_error_ticks = 0;
    set_connecting(false);
    start_scan();
}

void screen_wifi_refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_WIFI));
    lv_label_set_text(s_lbl_net, i18n_t(STR_WIFI_NETWORK));
    lv_label_set_text(s_lbl_pass, i18n_t(STR_WIFI_PASSWORD));

    if (!s_connecting) lv_label_set_text(s_lbl_conn, i18n_t(STR_WIFI_CONNECT));

    if (lv_obj_has_state(s_dd_network, LV_STATE_DISABLED)) {
        lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_SEARCHING));
    } else if (s_scan_count == 0) {
        lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_NO_NETWORKS));
    }
}

void screen_wifi_create(void) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_text_font(s_screen, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    wifi_set_on_error(on_auth_error);

    // ── Header (y=0 h=80) ─────────────────────────────────────────────────────
    s_header = lv_obj_create(s_screen);
    lv_obj_set_size(s_header, 480, 80);
    lv_obj_set_pos(s_header, 0, 0);
    lv_obj_set_style_radius(s_header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_header, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_header, lv_color_hex(0x2c3e50), 0);
    lv_obj_clear_flag(s_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(s_header);
    lv_obj_set_size(btn_back, 70, 70);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 6);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    ui_icon_create(btn_back, UI_SYMBOL_BACK, lv_color_white(), &lv_font_app_30);

    s_lbl_title = lv_label_create(s_header);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_WIFI));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_rescan = lv_btn_create(s_header);
    lv_obj_set_size(btn_rescan, 70, 70);
    lv_obj_align(btn_rescan, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_border_width(btn_rescan, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_rescan, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_rescan, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_rescan, 6);
    lv_obj_add_event_cb(btn_rescan, on_rescan, LV_EVENT_CLICKED, NULL);
    ui_icon_create(btn_rescan, UI_SYMBOL_REFRESH, lv_color_white(), &lv_font_app_30);

    // ── Status line ───────────────────────────────────────────────────────────
    s_lbl_status = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_status, 17, 125);
    lv_label_set_text(s_lbl_status, i18n_t(STR_WIFI_DISCONNECTED));

    // ── Network row (label-above/field-below; 580px field didn't fit 480px) ────
    s_lbl_net = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_net, &lv_font_app_24, LV_PART_MAIN);
    lv_label_set_text(s_lbl_net, i18n_t(STR_WIFI_NETWORK));
    lv_obj_set_pos(s_lbl_net, 17, 200);

    s_dd_network = lv_dropdown_create(s_screen);
    lv_obj_set_size(s_dd_network, 446, 57);
    lv_obj_set_pos(s_dd_network, 17, 225);
    lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_SEARCHING));
    lv_obj_add_state(s_dd_network, LV_STATE_DISABLED);
    lv_dropdown_set_dir(s_dd_network, LV_DIR_BOTTOM);

    // ── Password row (label-above, field+eye-button below) ─────────────────────
    s_lbl_pass = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_pass, &lv_font_app_24, LV_PART_MAIN);
    lv_label_set_text(s_lbl_pass, i18n_t(STR_WIFI_PASSWORD));
    lv_obj_set_pos(s_lbl_pass, 17, 300);

    s_ta_pass = lv_textarea_create(s_screen);
    lv_obj_set_size(s_ta_pass, 446, 57);
    lv_obj_set_pos(s_ta_pass, 17, 325);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_obj_set_scrollbar_mode(s_ta_pass, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_pass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(s_ta_pass, 11, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_ta_pass, 11, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ta_pass, on_pass_clicked, LV_EVENT_CLICKED, NULL);

    // ── Connect button ────────────────────────────────────────────────────────
    s_btn_conn = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_conn, 217, 54);
    lv_obj_set_pos(s_btn_conn, 131, 410);
    lv_obj_add_event_cb(s_btn_conn, on_connect, LV_EVENT_CLICKED, NULL);
    s_lbl_conn = lv_label_create(s_btn_conn);
    lv_label_set_text(s_lbl_conn, i18n_t(STR_WIFI_CONNECT));
    lv_obj_center(s_lbl_conn);

    // ── Auth-failure error banner ─────────────────────────────────────────────
    s_lbl_error = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_error, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_error, lv_color_make(255, 60, 60), LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_error, 17, 480);
    lv_label_set_text(s_lbl_error, "");
    lv_obj_add_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);

    // ── Always-running 1s status/error timer ─────────────────────────────────
    lv_timer_create(status_tick_cb, 1000, NULL);
}

void screen_wifi_load(void) {
    lv_scr_load(s_screen);
}
