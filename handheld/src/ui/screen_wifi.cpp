#include "screen_wifi.h"
#include "ui_manager.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "wifi/wifi_manager.h"
#include "display/display.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>
#include "fonts.h"

// ---------------------------------------------------------------------------
// Normal-mode layout (480×320 landscape)
//   Header:       y=0,   h=36
//   Status:       y=46,  montserrat_14
//   Network lbl:  x=10,  y=94   (centred with dropdown)
//   Network dd:   x=120, y=82,  w=348, h=38
//   Password lbl: x=10,  y=154  (centred with textarea)
//   Textarea:     x=120, y=142, w=298, h=38
//   Eye btn:      x=420, y=142, w=50,  h=38
//   Connect btn:  x=338, y=200, w=130, h=36
//   Error banner: x=10,  y=250  (red, hidden by default)
//
// Keyboard mode — activated when the password field is tapped:
//   All rows except the password label + field are hidden.
//   Password label: y=12, textarea+eye at y=40 h=50.
//   Keyboard: h=200 (≈50 px/row → finger-friendly).
// ---------------------------------------------------------------------------
#define MAX_APS  20
#define OPTS_BUF (MAX_APS * 34)

// ── Persistent widget refs ────────────────────────────────────────────────────
static lv_obj_t *s_screen     = NULL;
static lv_obj_t *s_header     = NULL;
static lv_obj_t *s_lbl_back   = NULL;
static lv_obj_t *s_lbl_title  = NULL;
static lv_obj_t *s_lbl_rescan = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_lbl_net    = NULL;
static lv_obj_t *s_dd_network = NULL;
static lv_obj_t *s_lbl_pass   = NULL;
static lv_obj_t *s_ta_pass    = NULL;
static lv_obj_t *s_btn_eye    = NULL;
static lv_obj_t *s_lbl_eye    = NULL;
static lv_obj_t *s_btn_conn   = NULL;
static lv_obj_t *s_lbl_conn   = NULL;
static lv_obj_t *s_lbl_error  = NULL;
static lv_obj_t *s_keyboard   = NULL;

// State flags
static bool s_pass_visible  = false;
static bool s_keyboard_mode = false;

// Connecting state — tracks in-progress connection attempt.
// connect btn is disabled + shows "Connecting..." until result or timeout.
static bool s_connecting       = false;
static int  s_connecting_ticks = 0;   // counts down each 1 s tick; 0 = idle

// Auth-error flag — set from WiFi event task, consumed by the 1 s LVGL timer.
static volatile bool s_auth_error  = false;
static int           s_error_ticks = 0;

// Scan results — written from WiFi event task, read in LVGL polling timer.
static wifi_ap_t     s_scan_aps[MAX_APS];
static uint16_t      s_scan_count = 0;
static volatile bool s_scan_ready = false;
static lv_timer_t   *s_scan_timer = NULL;

// ---------------------------------------------------------------------------
// Connect-button helpers
// ---------------------------------------------------------------------------
static void set_connecting(bool on)
{
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

// ---------------------------------------------------------------------------
// Scan helpers
// ---------------------------------------------------------------------------
static void on_scan_done(const wifi_ap_t *aps, uint16_t count)
{
    s_scan_count = count < MAX_APS ? count : MAX_APS;
    for (uint16_t i = 0; i < s_scan_count; i++) s_scan_aps[i] = aps[i];
    s_scan_ready = true;
}

static void update_dropdown(void)
{
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

static void scan_poll_cb(lv_timer_t *t)
{
    if (!s_scan_ready) return;
    s_scan_ready = false;
    s_scan_timer = NULL;
    update_dropdown();
    lv_timer_del(t);
}

static void start_scan(void)
{
    lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_SEARCHING));
    lv_obj_add_state(s_dd_network, LV_STATE_DISABLED);
    s_scan_ready = false;
    wifi_scan_start(on_scan_done);
    if (!s_scan_timer) s_scan_timer = lv_timer_create(scan_poll_cb, 200, NULL);
}

// ---------------------------------------------------------------------------
// Auth-error callback — WiFi event task context; flag only, no LVGL calls.
// ---------------------------------------------------------------------------
static void on_auth_error(void) { s_auth_error = true; }

// ---------------------------------------------------------------------------
// Status & error timer (1 s, always running)
// ---------------------------------------------------------------------------
static void status_tick_cb(lv_timer_t *t)
{
    (void)t;

    // Auth-error: re-enable button immediately and show banner.
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

    // Connecting state: re-enable button once connected or after timeout.
    if (s_connecting) {
        if (wifi_is_connected()) {
            set_connecting(false);
        } else if (--s_connecting_ticks <= 0) {
            set_connecting(false);   // timed out — let user retry
        }
    }

    // Connection status / IP display.
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

// ---------------------------------------------------------------------------
// Keyboard focus mode
// ---------------------------------------------------------------------------
static void enter_keyboard_mode(void)
{
    if (s_keyboard_mode) return;
    s_keyboard_mode = true;

    lv_obj_add_flag(s_header,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_net,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dd_network, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_conn,   LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_error,  LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_pos(s_lbl_pass, 10, 12);
    lv_obj_set_pos(s_ta_pass,  10, 40);
    lv_obj_set_size(s_ta_pass, 415, 50);
    lv_obj_set_pos(s_btn_eye,  427, 40);
    lv_obj_set_size(s_btn_eye, 46,  50);

    lv_obj_set_size(s_keyboard, 480, 200);
    lv_keyboard_set_textarea(s_keyboard, s_ta_pass);
    lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void exit_keyboard_mode(void)
{
    if (!s_keyboard_mode) return;
    s_keyboard_mode = false;

    lv_obj_clear_flag(s_header,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_net,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_dd_network, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_btn_conn,   LV_OBJ_FLAG_HIDDEN);
    // s_lbl_error visibility is owned by the timer — leave it alone.

    lv_obj_set_pos(s_lbl_pass, 10,  154);
    lv_obj_set_pos(s_ta_pass,  120, 142);
    lv_obj_set_size(s_ta_pass, 298, 38);
    lv_obj_set_pos(s_btn_eye,  420, 142);
    lv_obj_set_size(s_btn_eye, 50,  38);

    lv_obj_set_size(s_keyboard, 480, 120);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Event callbacks
// ---------------------------------------------------------------------------
static void on_back(lv_event_t *e)
{
    exit_keyboard_mode();
    ui_manager_show(SCREEN_SETTINGS);
}

static void on_rescan(lv_event_t *e) { start_scan(); }

static void on_eye_toggle(lv_event_t *e)
{
    s_pass_visible = !s_pass_visible;
    lv_textarea_set_password_mode(s_ta_pass, !s_pass_visible);
    lv_label_set_text(s_lbl_eye, s_pass_visible ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}

static void on_connect(lv_event_t *e)
{
    char ssid[33] = {};
    lv_dropdown_get_selected_str(s_dd_network, ssid, sizeof(ssid));
    if (ssid[0] == '\0' ||
        strcmp(ssid, i18n_t(STR_WIFI_SEARCHING))   == 0 ||
        strcmp(ssid, i18n_t(STR_WIFI_NO_NETWORKS)) == 0) return;

    set_connecting(true);
    wifi_set_credentials(ssid, lv_textarea_get_text(s_ta_pass));
}

static void on_pass_focused(lv_event_t *e)   { enter_keyboard_mode(); }
static void on_pass_defocused(lv_event_t *e)  { exit_keyboard_mode(); }

static void on_kb_done(lv_event_t *e)
{
    lv_obj_clear_state(s_ta_pass, LV_STATE_FOCUSED);
    exit_keyboard_mode();
}

static void on_screen_loaded(lv_event_t *e)
{
    s_pass_visible  = false;
    s_keyboard_mode = false;
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_label_set_text(s_lbl_eye, LV_SYMBOL_EYE_OPEN);
    lv_textarea_set_text(s_ta_pass, "");
    lv_obj_add_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);
    s_error_ticks = 0;
    set_connecting(false);   // ensure button is enabled on every visit
    start_scan();
}

// ---------------------------------------------------------------------------
// Language refresh
// ---------------------------------------------------------------------------
void screen_wifi_refresh_language(void)
{
    lv_label_set_text(s_lbl_back,   i18n_t(STR_BTN_BACK));
    lv_label_set_text(s_lbl_title,  i18n_t(STR_SETTINGS_WIFI));
    lv_label_set_text(s_lbl_rescan, i18n_t(STR_WIFI_RESCAN));
    lv_label_set_text(s_lbl_net,    i18n_t(STR_WIFI_NETWORK));
    lv_label_set_text(s_lbl_pass,   i18n_t(STR_WIFI_PASSWORD));

    // Only refresh the connect button text if not mid-connection.
    if (!s_connecting) lv_label_set_text(s_lbl_conn, i18n_t(STR_WIFI_CONNECT));

    if (lv_obj_has_state(s_dd_network, LV_STATE_DISABLED)) {
        lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_SEARCHING));
    } else if (s_scan_count == 0) {
        lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_NO_NETWORKS));
    }
}

// ---------------------------------------------------------------------------
// Screen creation
// ---------------------------------------------------------------------------
void screen_wifi_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_text_font(s_screen, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_screen, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    wifi_set_on_error(on_auth_error);

    // ── Header (y=0 h=36) ────────────────────────────────────────────────────
    s_header = lv_obj_create(s_screen);
    lv_obj_set_size(s_header, 480, 36);
    lv_obj_set_pos(s_header, 0, 0);
    lv_obj_set_style_radius(s_header, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_header, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *btn_back = lv_btn_create(s_header);
    lv_obj_set_size(btn_back, 70, 28);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, i18n_t(STR_BTN_BACK));
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(s_header);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_WIFI));
    lv_obj_set_style_text_font(s_lbl_title, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_rescan = lv_btn_create(s_header);
    lv_obj_set_size(btn_rescan, 90, 28);
    lv_obj_align(btn_rescan, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(btn_rescan, on_rescan, LV_EVENT_CLICKED, NULL);
    s_lbl_rescan = lv_label_create(btn_rescan);
    lv_label_set_text(s_lbl_rescan, i18n_t(STR_WIFI_RESCAN));
    lv_obj_center(s_lbl_rescan);

    // ── Status line (y=46) ───────────────────────────────────────────────────
    s_lbl_status = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_status, &pilocows_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_make(180, 180, 180), LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_status, 10, 46);
    lv_label_set_text(s_lbl_status, i18n_t(STR_WIFI_DISCONNECTED));

    // ── Network row (dd y=82 h=38, label centred at y=94) ───────────────────
    s_lbl_net = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_net, &pilocows_font_14, LV_PART_MAIN);
    lv_label_set_text(s_lbl_net, i18n_t(STR_WIFI_NETWORK));
    lv_obj_set_pos(s_lbl_net, 10, 94);

    s_dd_network = lv_dropdown_create(s_screen);
    lv_obj_set_size(s_dd_network, 348, 38);
    lv_obj_set_pos(s_dd_network, 120, 82);
    lv_dropdown_set_options(s_dd_network, i18n_t(STR_WIFI_SEARCHING));
    lv_obj_add_state(s_dd_network, LV_STATE_DISABLED);
    lv_dropdown_set_dir(s_dd_network, LV_DIR_BOTTOM);

    // ── Password row (textarea y=142 h=38, label centred at y=154) ──────────
    s_lbl_pass = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_pass, &pilocows_font_14, LV_PART_MAIN);
    lv_label_set_text(s_lbl_pass, i18n_t(STR_WIFI_PASSWORD));
    lv_obj_set_pos(s_lbl_pass, 10, 154);

    s_ta_pass = lv_textarea_create(s_screen);
    lv_obj_set_size(s_ta_pass, 298, 38);
    lv_obj_set_pos(s_ta_pass, 120, 142);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_password_mode(s_ta_pass, true);
    // Disable scrollbar — no need to scroll a password field.
    lv_obj_set_scrollbar_mode(s_ta_pass, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_pass, LV_OBJ_FLAG_SCROLLABLE);
    // Align content height with the dropdown above.
    lv_obj_set_style_pad_top(s_ta_pass, 7, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_ta_pass, 7, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ta_pass, on_pass_focused,   LV_EVENT_FOCUSED,   NULL);
    lv_obj_add_event_cb(s_ta_pass, on_pass_defocused, LV_EVENT_DEFOCUSED, NULL);

    s_btn_eye = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_eye, 50, 38);
    lv_obj_set_pos(s_btn_eye, 420, 142);
    lv_obj_add_event_cb(s_btn_eye, on_eye_toggle, LV_EVENT_CLICKED, NULL);
    s_lbl_eye = lv_label_create(s_btn_eye);
    lv_label_set_text(s_lbl_eye, LV_SYMBOL_EYE_OPEN);
    lv_obj_center(s_lbl_eye);

    // ── Connect button (y=200 h=36) ──────────────────────────────────────────
    s_btn_conn = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_conn, 130, 36);
    lv_obj_set_pos(s_btn_conn, 338, 200);
    lv_obj_add_event_cb(s_btn_conn, on_connect, LV_EVENT_CLICKED, NULL);
    s_lbl_conn = lv_label_create(s_btn_conn);
    lv_label_set_text(s_lbl_conn, i18n_t(STR_WIFI_CONNECT));
    lv_obj_center(s_lbl_conn);

    // ── Auth-failure error banner (y=250, hidden by default) ─────────────────
    s_lbl_error = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_lbl_error, &pilocows_font_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_error, lv_color_make(255, 60, 60), LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_error, 10, 250);
    lv_label_set_text(s_lbl_error, "");
    lv_obj_add_flag(s_lbl_error, LV_OBJ_FLAG_HIDDEN);

    // ── On-screen keyboard (bottom, hidden; expands to h=200 in keyboard mode) ─
    s_keyboard = lv_keyboard_create(s_screen);
    lv_obj_set_size(s_keyboard, 480, 120);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, on_kb_done, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_keyboard, on_kb_done, LV_EVENT_CANCEL, NULL);

    // ── Always-running 1 s status/error timer ────────────────────────────────
    lv_timer_create(status_tick_cb, 1000, NULL);
}

void screen_wifi_load(void)
{
    lv_scr_load(s_screen);
}
