#include "screen_scan.h"
#include "ui_manager.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "storage/session_storage.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

// ── Layout constants ──────────────────────────────────────────────────────────
// 480 × 320 landscape
//
//  y=  0, h=60  Header — 2 rows, back button spans full height
//               row 1 (h=30): session name centred
//               row 2 (h=30): clock left | tag count right
//  y= 60, h=58  EID area — 2 centred rows
//               row 1 (h=30): EID value (montserrat_26)
//               row 2 (h=28): status badge (new animal / already scanned / ready)
//  y=118, h=152 Data panel  (type-specific widgets)
//  y=270, h=50  Note textarea  (montserrat_20 needs ≥50px to avoid vertical scroll)
//
//  Flash overlay: full 480×320, z-order top, hidden by default.

// ── Internal state ────────────────────────────────────────────────────────────
static session_meta_t  s_session;          // copy of current session meta
static bool            s_has_session  = false;
static char            s_current_eid[SESSION_EID_MAX + 1] = {0};
static bool            s_eid_pending  = false;

// Type-specific state
static uint16_t        s_weight_kg    = 0;   // last weight used in this session
static uint32_t        s_weight_session_id = 0; // session that owns s_weight_kg (0 = none)
static pregnancy_result_t s_preg      = PREGNANCY_UNKNOWN;
static tb_result_t     s_tb           = TB_INCONCLUSIVE;

// ── Screen root ───────────────────────────────────────────────────────────────
static lv_obj_t *s_scr = NULL;

// ── Header ────────────────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_sess_name;   // session name (row 1, centred)
static lv_obj_t *s_lbl_clock;       // date/time (row 2, left)
static lv_obj_t *s_lbl_hdr_count;   // tag count (row 2, right)

// ── EID area ──────────────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_eid;
static lv_obj_t *s_lbl_status_tag;  // "New animal" / "Already scanned" / "Ready to scan"

// ── Data panel and its sub-panels (one shown at a time) ───────────────────────
static lv_obj_t *s_data_panel;
static lv_obj_t *s_panel_none;        // General / Removal — hint label
static lv_obj_t *s_lbl_hint;

static lv_obj_t *s_panel_weighing;
static lv_obj_t *s_lbl_weight_title;
static lv_obj_t *s_lbl_weight_val;   // shows "NNN kg"
static lv_obj_t *s_btn_w_minus100;
static lv_obj_t *s_btn_w_minus10;
static lv_obj_t *s_btn_w_minus;
static lv_obj_t *s_btn_w_plus;
static lv_obj_t *s_btn_w_plus10;
static lv_obj_t *s_btn_w_plus100;
static lv_obj_t *s_lbl_wunit;

static lv_obj_t *s_panel_preg;
static lv_obj_t *s_lbl_preg_title;
static lv_obj_t *s_btn_preg[3];       // PREGNANCY_UNKNOWN, YES, NO
static lv_obj_t *s_lbl_preg[3];

static lv_obj_t *s_panel_tb;
static lv_obj_t *s_lbl_tb_title;
static lv_obj_t *s_btn_tb[3];         // TB_INCONCLUSIVE, POSITIVE, NEGATIVE
static lv_obj_t *s_lbl_tb[3];

static lv_obj_t *s_panel_vax;
static lv_obj_t *s_lbl_vax_title;
static lv_obj_t *s_lbl_vax_list;      // comma-separated vaccine names

// ── Note field ────────────────────────────────────────────────────────────────
static lv_obj_t *s_ta_note;
static lv_obj_t *s_kb_note;
static lv_obj_t *s_lbl_kb_note_title;  // "Note" shown above textarea during kb mode

// ── No-session overlay ────────────────────────────────────────────────────────
static lv_obj_t *s_no_session_panel;
static lv_obj_t *s_lbl_no_session;
static lv_obj_t *s_lbl_btn_go_sessions;

// ── Flash overlay ─────────────────────────────────────────────────────────────
static lv_obj_t *s_flash_overlay;

// ── Timers ────────────────────────────────────────────────────────────────────
static lv_timer_t *s_flash_timer  = NULL;
static lv_timer_t *s_clock_timer  = NULL;
static lv_timer_t *s_status_timer = NULL;

// ── Keyboard mode (note field focused) ───────────────────────────────────────
static bool s_kb_active = false;
// Objects to hide during keyboard mode
static lv_obj_t *s_hdr = NULL;
static lv_obj_t *s_hide_in_kb[] = { NULL, NULL, NULL, NULL, NULL };  // hdr, bar, eid row, data panel, sentinel

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
static void clock_tick_cb(lv_timer_t *t);
static void hide_flash_cb(lv_timer_t *t);
static void clear_status_cb(lv_timer_t *t);
static void show_data_panel_for_type(uint8_t type);
static void update_weight_label(void);
static void update_preg_buttons(void);
static void update_tb_buttons(void);
static void enter_kb_mode(void);
static void exit_kb_mode(void);

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────


static void update_session_bar(void)
{
    if (!s_has_session) {
        lv_label_set_text(s_lbl_sess_name, i18n_t(STR_SCAN_NO_SESSION));
        lv_label_set_text(s_lbl_hdr_count, "0");
        return;
    }
    lv_label_set_text(s_lbl_sess_name, s_session.name);
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%" PRIu32, s_session.tag_count);
    lv_label_set_text(s_lbl_hdr_count, cnt);
}

static void update_weight_label(void)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u kg", (unsigned)s_weight_kg);
    lv_label_set_text(s_lbl_weight_val, buf);
}

static void update_preg_buttons(void)
{
    for (int i = 0; i < 3; i++) {
        bool sel = (s_preg == (pregnancy_result_t)i);
        lv_obj_set_style_bg_color(s_btn_preg[i],
            sel ? lv_palette_main(LV_PALETTE_BLUE)
                : lv_palette_lighten(LV_PALETTE_BLUE, 4),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_btn_preg[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_lbl_preg[i],
            sel ? lv_color_white() : lv_color_black(),
            LV_PART_MAIN);
    }
}

static void update_tb_buttons(void)
{
    for (int i = 0; i < 3; i++) {
        bool sel = (s_tb == (tb_result_t)i);
        lv_obj_set_style_bg_color(s_btn_tb[i],
            sel ? lv_palette_main(LV_PALETTE_BLUE)
                : lv_palette_lighten(LV_PALETTE_BLUE, 4),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_btn_tb[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_lbl_tb[i],
            sel ? lv_color_white() : lv_color_black(),
            LV_PART_MAIN);
    }
}

static void show_data_panel_for_type(uint8_t type)
{
    lv_obj_add_flag(s_panel_none,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_preg,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_tb,       LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_vax,      LV_OBJ_FLAG_HIDDEN);

    switch ((session_type_t)type) {
        case SESSION_TYPE_WEIGHING:    lv_obj_clear_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN); break;
        case SESSION_TYPE_VACCINATION: lv_obj_clear_flag(s_panel_vax,      LV_OBJ_FLAG_HIDDEN); break;
        case SESSION_TYPE_PREGNANCY:   lv_obj_clear_flag(s_panel_preg,     LV_OBJ_FLAG_HIDDEN); break;
        case SESSION_TYPE_TB_TEST:     lv_obj_clear_flag(s_panel_tb,       LV_OBJ_FLAG_HIDDEN); break;
        default:                       lv_obj_clear_flag(s_panel_none,     LV_OBJ_FLAG_HIDDEN); break;
    }
}

// Fill the vaccination panel with vaccine names from the current session.
static void populate_vax_panel(void)
{
    if (!s_has_session || s_session.vax_count == 0) {
        lv_label_set_text(s_lbl_vax_list, "—");
        return;
    }
    char buf[128] = {0};
    for (int i = 0; i < s_session.vax_count; i++) {
        char name[VACCINE_NAME_MAX];
        if (vaccine_get_name(s_session.vax_ids[i], name, sizeof(name))) {
            if (i > 0) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, name, sizeof(buf) - strlen(buf) - 1);
        }
    }
    lv_label_set_text(s_lbl_vax_list, buf[0] ? buf : "—");
}

// Enable or disable all data-entry controls.
// Controls stay disabled until the first tag is scanned (s_eid_pending becomes true).
static void update_controls_enabled(bool enabled)
{
    lv_state_t state = enabled ? 0 : LV_STATE_DISABLED;

    // Weight buttons (all 6)
    lv_obj_t *w_btns[] = {
        s_btn_w_minus100, s_btn_w_minus10, s_btn_w_minus,
        s_btn_w_plus, s_btn_w_plus10, s_btn_w_plus100,
    };
    for (int i = 0; i < 6; i++) {
        if (enabled) lv_obj_clear_state(w_btns[i], LV_STATE_DISABLED);
        else         lv_obj_add_state  (w_btns[i], LV_STATE_DISABLED);
    }

    // Pregnancy buttons
    for (int i = 0; i < 3; i++) {
        if (enabled) lv_obj_clear_state(s_btn_preg[i], LV_STATE_DISABLED);
        else         lv_obj_add_state  (s_btn_preg[i], LV_STATE_DISABLED);
    }

    // TB buttons
    for (int i = 0; i < 3; i++) {
        if (enabled) lv_obj_clear_state(s_btn_tb[i], LV_STATE_DISABLED);
        else         lv_obj_add_state  (s_btn_tb[i], LV_STATE_DISABLED);
    }

    // Note textarea
    if (enabled) {
        lv_obj_clear_state(s_ta_note, LV_STATE_DISABLED);
        lv_obj_add_flag   (s_ta_note, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_state  (s_ta_note, LV_STATE_DISABLED);
        lv_obj_clear_flag (s_ta_note, LV_OBJ_FLAG_CLICKABLE);
    }

    (void)state;
}

static void enter_kb_mode(void)
{
    if (s_kb_active) return;
    s_kb_active = true;
    for (int i = 0; s_hide_in_kb[i]; i++) lv_obj_add_flag(s_hide_in_kb[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_kb_note_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_kb_note, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb_note, s_ta_note);
    lv_obj_set_pos(s_ta_note, 8, 60);
    lv_obj_set_width(s_ta_note, 464);
}

static void exit_kb_mode(void)
{
    if (!s_kb_active) return;
    s_kb_active = false;
    for (int i = 0; s_hide_in_kb[i]; i++) lv_obj_clear_flag(s_hide_in_kb[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_kb_note_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_kb_note, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_ta_note, 8, 270);
    lv_obj_set_width(s_ta_note, 464);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e)
{
    (void)e;
    exit_kb_mode();
    // Auto-save pending tag before leaving the screen.
    // Called from an LVGL event callback — the LVGL lock is already held,
    // so we read UI fields directly and do the (small) file write inline.
    if (s_eid_pending && s_has_session) {
        tag_record_t rec;
        if (screen_scan_get_record(&rec)) {
            session_save_record(&rec);
        }
        screen_scan_clear_pending();
    }
    ui_manager_show(SCREEN_SESSION_MENU);
}


static void on_go_sessions(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SESSION_MENU);
}

static void on_w_minus(lv_event_t *e)
{
    (void)e;
    if (s_weight_kg > 0) s_weight_kg--;
    update_weight_label();
}

static void on_w_plus(lv_event_t *e)
{
    (void)e;
    if (s_weight_kg < 999) s_weight_kg++;
    update_weight_label();
}

static void on_w_minus_long(lv_event_t *e)
{
    (void)e;
    s_weight_kg = (s_weight_kg >= 10) ? s_weight_kg - 10 : 0;
    update_weight_label();
}

static void on_w_plus_long(lv_event_t *e)
{
    (void)e;
    s_weight_kg = (s_weight_kg <= 989) ? s_weight_kg + 10 : 999;
    update_weight_label();
}

static void on_w_minus100(lv_event_t *e)
{
    (void)e;
    s_weight_kg = (s_weight_kg >= 100) ? s_weight_kg - 100 : 0;
    update_weight_label();
}

static void on_w_plus100(lv_event_t *e)
{
    (void)e;
    s_weight_kg = (s_weight_kg <= 899) ? s_weight_kg + 100 : 999;
    update_weight_label();
}

static void on_preg_btn(lv_event_t *e)
{
    s_preg = (pregnancy_result_t)(intptr_t)lv_event_get_user_data(e);
    update_preg_buttons();
}

static void on_tb_btn(lv_event_t *e)
{
    s_tb = (tb_result_t)(intptr_t)lv_event_get_user_data(e);
    update_tb_buttons();
}

static void on_note_focused(lv_event_t *e)
{
    (void)e;
    enter_kb_mode();
}

static void on_note_clicked(lv_event_t *e)
{
    (void)e;
    // Re-open keyboard if it was dismissed but textarea still has cursor focus
    if (!s_kb_active) {
        enter_kb_mode();
    }
}

static void on_kb_ready(lv_event_t *e)
{
    (void)e;
    exit_kb_mode();
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;
    exit_kb_mode();
    // Refresh session state in case it changed since last visit
    session_meta_t m;
    if (session_get_active(&m)) {
        screen_scan_set_session(&m);
    } else {
        screen_scan_set_session(NULL);
    }
}

static void hide_flash_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_add_flag(s_flash_overlay, LV_OBJ_FLAG_HIDDEN);
    s_flash_timer = NULL;
}

static void set_status_ready(void)
{
    lv_label_set_text(s_lbl_status_tag, i18n_t(STR_SCAN_READY));
    lv_obj_set_style_bg_color(s_lbl_status_tag,  lv_color_hex(0xFF00FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lbl_status_tag,    LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status_tag, lv_color_white(),       LV_PART_MAIN);
}

static void clear_status_cb(lv_timer_t *t)
{
    (void)t;
    set_status_ready();
    s_status_timer = NULL;
}

static void clock_tick_cb(lv_timer_t *t)
{
    (void)t;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buf[20];
    strftime(buf, sizeof(buf), "%d %b %H:%M", tm_info);
    lv_label_set_text(s_lbl_clock, buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: make a radio-style button
// ─────────────────────────────────────────────────────────────────────────────

static lv_obj_t *make_radio_btn(lv_obj_t *parent, int x, int y, int w, int h,
                                 const char *text, lv_event_cb_t cb, void *ud,
                                 lv_obj_t **lbl_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_palette_lighten(LV_PALETTE_BLUE, 4), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 5, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn, 10);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, ud);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_black(), LV_PART_MAIN);
    lv_obj_center(lbl);
    if (lbl_out) *lbl_out = lbl;

    return btn;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────

void screen_scan_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header (y=0 h=60) — 2 rows, back button spans full height ────────────
    s_hdr = lv_obj_create(s_scr);
    lv_obj_set_size(s_hdr, 480, 60);
    lv_obj_set_pos(s_hdr, 0, 0);
    lv_obj_clear_flag(s_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_hdr, 0, LV_PART_MAIN);

    // Back button — spans both rows
    lv_obj_t *btn_back = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_back, 52, 56);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 2, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 6);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_back, &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_center(lbl_back);

    // Row 1: session name — centred between back button and right edge
    s_lbl_sess_name = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_sess_name, "");
    lv_label_set_long_mode(s_lbl_sess_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_sess_name, 390);
    lv_obj_set_style_text_font(s_lbl_sess_name, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_sess_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_sess_name, 56, 6);

    // Row 2: clock (left) + tag count (right)
    s_lbl_clock = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_clock, "-- --- --:--");
    lv_obj_set_style_text_font(s_lbl_clock, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_clock, 58, 38);

    s_lbl_hdr_count = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_hdr_count, "0");
    lv_obj_set_width(s_lbl_hdr_count, 100);
    lv_obj_set_style_text_font(s_lbl_hdr_count, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_hdr_count, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_hdr_count, 374, 38);  // 480 - 100 - 6

    // ── EID area (y=60 h=58) — 2 centred rows ─────────────────────────────────
    lv_obj_t *eid_row = lv_obj_create(s_scr);
    lv_obj_set_size(eid_row, 480, 58);
    lv_obj_set_pos(eid_row, 0, 60);
    lv_obj_clear_flag(eid_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(eid_row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(eid_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(eid_row, 0, LV_PART_MAIN);

    // Row 1: EID value — centred, larger font
    s_lbl_eid = lv_label_create(eid_row);
    lv_label_set_text(s_lbl_eid, "---");
    lv_obj_set_style_text_font(s_lbl_eid, &lv_font_montserrat_26, LV_PART_MAIN);
    lv_obj_align(s_lbl_eid, LV_ALIGN_TOP_MID, 0, 2);

    // Row 2: status badge — centred
    s_lbl_status_tag = lv_label_create(eid_row);
    lv_obj_set_style_text_font(s_lbl_status_tag, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_radius(s_lbl_status_tag, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_lbl_status_tag, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_lbl_status_tag, 3, LV_PART_MAIN);
    lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -2);
    set_status_ready();

    // ── Data panel (y=118 h=152) ──────────────────────────────────────────────
    // Height ends at y=270 to leave room for the note textarea below.
    s_data_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_data_panel, 480, 152);
    lv_obj_set_pos(s_data_panel, 0, 118);
    lv_obj_clear_flag(s_data_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_data_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_data_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_data_panel, 8, LV_PART_MAIN);

    // ── Panel: General / Removal ───────────────────────────────────────────────
    s_panel_none = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_none, 464, 136);
    lv_obj_set_pos(s_panel_none, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_none, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_none, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_none, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_hint = lv_label_create(s_panel_none);
    lv_label_set_text(s_lbl_hint, i18n_t(STR_SCAN_READY));
    lv_obj_set_style_text_font(s_lbl_hint, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_hint, LV_ALIGN_CENTER, 0, 0);

    // ── Panel: Weighing ────────────────────────────────────────────────────────
    // Layout: title at y=0, then 5 controls across 464px at y=22:
    //   [-10](76) gap(17) [-1](76) gap(17) [val](92) gap(17) [+1](76) gap(17) [+10](76)
    //   Total: 4×76 + 92 + 4×17 = 304 + 92 + 68 = 464 ✓
    s_panel_weighing = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_weighing, 464, 136);
    lv_obj_set_pos(s_panel_weighing, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_weighing, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_weighing, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_weighing, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_weighing, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_weight_title = lv_label_create(s_panel_weighing);
    lv_label_set_text(s_lbl_weight_title, i18n_t(STR_WEIGHT_KG));
    lv_obj_set_style_text_font(s_lbl_weight_title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_weight_title, 0, 0);

    // Button row: -100 | -10 | -1 | [value] | +1 | +10 | +100
    // Each button 62px wide, val_box 68px, 4px gaps → 7×62+68-62+6×4 = 464px total

    // -100 button
    s_btn_w_minus100 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus100, 62, 56);
    lv_obj_set_pos(s_btn_w_minus100, 0, 22);
    lv_obj_set_style_radius(s_btn_w_minus100, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus100, 10);
    lv_obj_add_event_cb(s_btn_w_minus100, on_w_minus100, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_m100 = lv_label_create(s_btn_w_minus100);
    lv_label_set_text(lbl_m100, "-100");
    lv_obj_set_style_text_font(lbl_m100, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(lbl_m100);

    // -10 button
    s_btn_w_minus10 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus10, 62, 56);
    lv_obj_set_pos(s_btn_w_minus10, 66, 22);
    lv_obj_set_style_radius(s_btn_w_minus10, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus10, 10);
    lv_obj_add_event_cb(s_btn_w_minus10, on_w_minus_long, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_m10 = lv_label_create(s_btn_w_minus10);
    lv_label_set_text(lbl_m10, "-10");
    lv_obj_set_style_text_font(lbl_m10, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(lbl_m10);

    // -1 button
    s_btn_w_minus = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus, 62, 56);
    lv_obj_set_pos(s_btn_w_minus, 132, 22);
    lv_obj_set_style_radius(s_btn_w_minus, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus, 10);
    lv_obj_add_event_cb(s_btn_w_minus, on_w_minus, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_minus = lv_label_create(s_btn_w_minus);
    lv_label_set_text(lbl_minus, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(lbl_minus, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(lbl_minus);

    // Value display
    lv_obj_t *val_box = lv_obj_create(s_panel_weighing);
    lv_obj_set_size(val_box, 68, 56);
    lv_obj_set_pos(val_box, 198, 22);
    lv_obj_set_style_border_width(val_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(val_box, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(val_box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(val_box, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_weight_val = lv_label_create(val_box);
    lv_label_set_text(s_lbl_weight_val, "0 kg");
    lv_obj_set_style_text_font(s_lbl_weight_val, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_weight_val, LV_ALIGN_CENTER, 0, 0);

    // +1 button
    s_btn_w_plus = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus, 62, 56);
    lv_obj_set_pos(s_btn_w_plus, 270, 22);
    lv_obj_set_style_radius(s_btn_w_plus, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus, 10);
    lv_obj_add_event_cb(s_btn_w_plus, on_w_plus, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_plus = lv_label_create(s_btn_w_plus);
    lv_label_set_text(lbl_plus, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(lbl_plus, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(lbl_plus);

    // +10 button
    s_btn_w_plus10 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus10, 62, 56);
    lv_obj_set_pos(s_btn_w_plus10, 336, 22);
    lv_obj_set_style_radius(s_btn_w_plus10, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus10, 10);
    lv_obj_add_event_cb(s_btn_w_plus10, on_w_plus_long, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_p10 = lv_label_create(s_btn_w_plus10);
    lv_label_set_text(lbl_p10, "+10");
    lv_obj_set_style_text_font(lbl_p10, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(lbl_p10);

    // +100 button
    s_btn_w_plus100 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus100, 62, 56);
    lv_obj_set_pos(s_btn_w_plus100, 402, 22);
    lv_obj_set_style_radius(s_btn_w_plus100, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus100, 10);
    lv_obj_add_event_cb(s_btn_w_plus100, on_w_plus100, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_p100 = lv_label_create(s_btn_w_plus100);
    lv_label_set_text(lbl_p100, "+100");
    lv_obj_set_style_text_font(lbl_p100, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(lbl_p100);

    // ── Panel: Pregnancy ───────────────────────────────────────────────────────
    s_panel_preg = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_preg, 464, 78);
    lv_obj_set_pos(s_panel_preg, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_preg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_preg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_preg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_preg, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_preg_title = lv_label_create(s_panel_preg);
    lv_label_set_text(s_lbl_preg_title, i18n_t(STR_PREG_RESULT));
    lv_obj_set_style_text_font(s_lbl_preg_title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_preg_title, 0, 0);

    const char *preg_labels[] = {
        i18n_t(STR_PREG_UNKNOWN), i18n_t(STR_PREG_YES), i18n_t(STR_PREG_NO)
    };
    for (int i = 0; i < 3; i++) {
        s_btn_preg[i] = make_radio_btn(s_panel_preg,
            4 + i * 154, 26, 146, 44,
            preg_labels[i], on_preg_btn, (void *)(intptr_t)i,
            &s_lbl_preg[i]);
    }
    update_preg_buttons();

    // ── Panel: TB Test ─────────────────────────────────────────────────────────
    s_panel_tb = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_tb, 464, 78);
    lv_obj_set_pos(s_panel_tb, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_tb, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_tb, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_tb, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_tb, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_tb_title = lv_label_create(s_panel_tb);
    lv_label_set_text(s_lbl_tb_title, i18n_t(STR_TB_RESULT));
    lv_obj_set_style_text_font(s_lbl_tb_title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_tb_title, 0, 0);

    const char *tb_labels[] = {
        i18n_t(STR_TB_INCONCLUSIVE), i18n_t(STR_TB_POSITIVE), i18n_t(STR_TB_NEGATIVE)
    };
    for (int i = 0; i < 3; i++) {
        s_btn_tb[i] = make_radio_btn(s_panel_tb,
            4 + i * 154, 26, 146, 44,
            tb_labels[i], on_tb_btn, (void *)(intptr_t)i,
            &s_lbl_tb[i]);
    }
    update_tb_buttons();

    // ── Panel: Vaccination ────────────────────────────────────────────────────
    s_panel_vax = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_vax, 464, 76);
    lv_obj_set_pos(s_panel_vax, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_vax, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_vax, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_vax, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_vax_title = lv_label_create(s_panel_vax);
    lv_label_set_text(s_lbl_vax_title, i18n_t(STR_SESSION_SELECT_VAX));
    lv_obj_set_style_text_font(s_lbl_vax_title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_title, 0, 0);

    s_lbl_vax_list = lv_label_create(s_panel_vax);
    lv_label_set_text(s_lbl_vax_list, "—");
    lv_label_set_long_mode(s_lbl_vax_list, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_vax_list, 460);
    lv_obj_set_style_text_font(s_lbl_vax_list, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_list, 0, 24);

    // Initial state: all hidden until session is set
    lv_obj_add_flag(s_panel_none,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_preg,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_tb,       LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_vax,      LV_OBJ_FLAG_HIDDEN);

    // ── Note textarea (y=270 h=50, bottom of screen) ─────────────────────────
    s_ta_note = lv_textarea_create(s_scr);
    lv_obj_set_size(s_ta_note, 464, 50);
    lv_obj_set_pos(s_ta_note, 8, 270);
    lv_textarea_set_one_line(s_ta_note, false);
    lv_textarea_set_max_length(s_ta_note, SESSION_NOTE_MAX - 1);
    lv_textarea_set_placeholder_text(s_ta_note, i18n_t(STR_NOTE_PLACEHOLDER));
    lv_obj_set_scrollbar_mode(s_ta_note, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_note, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(s_ta_note, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_ta_note, 15);
    lv_obj_add_event_cb(s_ta_note, on_note_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_note, on_note_clicked, LV_EVENT_CLICKED, NULL);

    // ── "Note" title shown above textarea during keyboard mode ────────────────
    s_lbl_kb_note_title = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_kb_note_title, i18n_t(STR_NOTE));
    lv_obj_set_style_text_font(s_lbl_kb_note_title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_kb_note_title, 12, 18);
    lv_obj_add_flag(s_lbl_kb_note_title, LV_OBJ_FLAG_HIDDEN);

    // ── Note keyboard (hidden, full-width bottom) ──────────────────────────────
    s_kb_note = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb_note, 480, 200);
    lv_obj_align(s_kb_note, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb_note, &lv_font_montserrat_22, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb_note, on_kb_ready, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb_note, on_kb_ready, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_kb_note, LV_OBJ_FLAG_HIDDEN);

    // Objects hidden during keyboard mode
    s_hide_in_kb[0] = s_hdr;
    s_hide_in_kb[1] = eid_row;
    s_hide_in_kb[2] = s_data_panel;

    // ── No-session overlay ────────────────────────────────────────────────────
    s_no_session_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_no_session_panel, 480, 260);
    lv_obj_set_pos(s_no_session_panel, 0, 60);
    lv_obj_clear_flag(s_no_session_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_no_session_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_no_session_panel, 0, LV_PART_MAIN);

    s_lbl_no_session = lv_label_create(s_no_session_panel);
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SCAN_NO_SESSION));
    lv_obj_set_style_text_font(s_lbl_no_session, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_no_session, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *btn_go = lv_btn_create(s_no_session_panel);
    lv_obj_set_size(btn_go, 240, 44);
    lv_obj_align(btn_go, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_radius(btn_go, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_go, 10);
    lv_obj_add_event_cb(btn_go, on_go_sessions, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_go_sessions = lv_label_create(btn_go);
    lv_label_set_text(s_lbl_btn_go_sessions, i18n_t(STR_SESSION_TITLE));
    lv_obj_set_style_text_font(s_lbl_btn_go_sessions, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_go_sessions);

    lv_obj_add_flag(s_no_session_panel, LV_OBJ_FLAG_HIDDEN);

    // ── Flash overlay ─────────────────────────────────────────────────────────
    s_flash_overlay = lv_obj_create(s_scr);
    lv_obj_set_size(s_flash_overlay, 480, 320);
    lv_obj_align(s_flash_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(s_flash_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_flash_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_flash_overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_flash_overlay, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    lv_obj_add_flag(s_flash_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_flash_overlay, LV_OBJ_FLAG_CLICKABLE);

    // ── Clock timer ───────────────────────────────────────────────────────────
    s_clock_timer = lv_timer_create(clock_tick_cb, 1000, NULL);
    clock_tick_cb(NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void screen_scan_load(void)
{
    lv_scr_load(s_scr);
    // on_screen_loaded fires via LV_EVENT_SCREEN_LOADED
}

void screen_scan_set_session(const session_meta_t *meta)
{
    if (meta) {
        bool new_session = (!s_has_session || s_session.id != meta->id);
        s_session     = *meta;
        s_has_session = true;
        // Count is updated via update_session_bar() at the end of this function
        // Default weight to 100 kg when entering a new weighing session
        if (new_session && meta->type == SESSION_TYPE_WEIGHING) {
            s_weight_kg = 100;
            s_weight_session_id = meta->id;
            update_weight_label();
        }
        lv_obj_add_flag(s_no_session_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ta_note,    LV_OBJ_FLAG_HIDDEN);
        show_data_panel_for_type(meta->type);
        populate_vax_panel();
    } else {
        s_has_session = false;
        lv_obj_clear_flag(s_no_session_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ta_note,    LV_OBJ_FLAG_HIDDEN);
    }
    update_session_bar();
    screen_scan_clear_pending();
}

void screen_scan_show_tag(const char *eid, bool is_duplicate)
{
    strlcpy(s_current_eid, eid, sizeof(s_current_eid));
    s_eid_pending = true;

    // Update EID display
    lv_label_set_text(s_lbl_eid, eid);

    // Status badge — filled rounded rect with white text
    if (is_duplicate) {
        lv_label_set_text(s_lbl_status_tag, i18n_t(STR_SCAN_DUPLICATE));
        lv_obj_set_style_bg_color(s_lbl_status_tag, lv_palette_main(LV_PALETTE_RED),   LV_PART_MAIN);
    } else {
        lv_label_set_text(s_lbl_status_tag, i18n_t(STR_SCAN_NEW_TAG));
        lv_obj_set_style_bg_color(s_lbl_status_tag, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    }
    lv_obj_set_style_bg_opa(s_lbl_status_tag,   LV_OPA_COVER,    LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status_tag, lv_color_white(), LV_PART_MAIN);

    // Keep the General/Removal data panel hint in sync — it is the most
    // prominent text on screen for those session types.
    lv_label_set_text(s_lbl_hint,
        is_duplicate ? i18n_t(STR_SCAN_DUPLICATE) : i18n_t(STR_SCAN_NEW_TAG));

    // Unlock data-entry controls now that a tag is pending
    update_controls_enabled(true);

    // For a new tag, optimistically bump the count so the UI updates immediately.
    // screen_scan_update_count() will confirm the real value after the record is saved.
    if (!is_duplicate && s_has_session) {
        s_session.tag_count++;
        update_session_bar();
    }

    // Flash
    screen_scan_flash(is_duplicate);

    if (is_duplicate && s_has_session) {
        // Pre-fill data fields from the existing session record
        tag_record_t rec;
        if (session_find_record(eid, &rec)) {
            // Restore note
            lv_textarea_set_text(s_ta_note, rec.note);

            uint8_t type = s_session.type;
            if (type == SESSION_TYPE_WEIGHING) {
                uint16_t w;
                memcpy(&w, rec.data, sizeof(w));
                s_weight_kg = w;
                update_weight_label();
            } else if (type == SESSION_TYPE_PREGNANCY) {
                s_preg = (pregnancy_result_t)rec.data[0];
                update_preg_buttons();
            } else if (type == SESSION_TYPE_TB_TEST) {
                s_tb = (tb_result_t)rec.data[0];
                update_tb_buttons();
            }
        }
    } else {
        // New tag — reset data fields, keep last weight for weighing
        lv_textarea_set_text(s_ta_note, "");
        if (s_has_session) {
            uint8_t type = s_session.type;
            if (type == SESSION_TYPE_PREGNANCY) {
                s_preg = PREGNANCY_UNKNOWN;
                update_preg_buttons();
            } else if (type == SESSION_TYPE_TB_TEST) {
                s_tb = TB_INCONCLUSIVE;
                update_tb_buttons();
            }
            // Weighing: s_weight_kg intentionally kept from last scan
        }
    }
}

bool screen_scan_get_record(tag_record_t *out)
{
    if (!out || !s_eid_pending || !s_has_session) return false;

    memset(out, 0, sizeof(*out));
    strlcpy(out->eid, s_current_eid, sizeof(out->eid));
    out->scanned_at = time(NULL);

    // Capture note
    const char *note = lv_textarea_get_text(s_ta_note);
    if (note) strlcpy(out->note, note, sizeof(out->note));

    // Type-specific data payload
    switch ((session_type_t)s_session.type) {
        case SESSION_TYPE_WEIGHING: {
            memcpy(out->data, &s_weight_kg, sizeof(s_weight_kg));
            break;
        }
        case SESSION_TYPE_PREGNANCY: {
            out->data[0] = (uint8_t)s_preg;
            break;
        }
        case SESSION_TYPE_TB_TEST: {
            out->data[0] = (uint8_t)s_tb;
            break;
        }
        case SESSION_TYPE_VACCINATION: {
            out->data[0] = s_session.vax_count;
            memcpy(out->data + 1, s_session.vax_ids,
                   s_session.vax_count < SESSION_DATA_SIZE - 1
                   ? s_session.vax_count : SESSION_DATA_SIZE - 1);
            break;
        }
        case SESSION_TYPE_REMOVAL: {
            // removal_date = scanned_at (preserved from first scan via duplicate pre-fill)
            time_t t = out->scanned_at;
            memcpy(out->data, &t, sizeof(t));
            break;
        }
        default: break;
    }
    return true;
}

void screen_scan_clear_pending(void)
{
    s_current_eid[0] = '\0';
    s_eid_pending     = false;
    lv_label_set_text(s_lbl_eid, "---");
    set_status_ready();
    lv_label_set_text(s_lbl_hint, i18n_t(STR_SCAN_READY));
    lv_textarea_set_text(s_ta_note, "");
    // Reset data fields
    s_preg = PREGNANCY_UNKNOWN;
    s_tb   = TB_INCONCLUSIVE;
    update_preg_buttons();
    update_tb_buttons();
    // Do NOT reset weight — persists within session
    // Lock data-entry controls until next scan
    update_controls_enabled(false);
}

void screen_scan_update_count(uint32_t count)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%" PRIu32, count);
    lv_label_set_text(s_lbl_hdr_count, buf);
    if (s_has_session) {
        s_session.tag_count = count;
    }
}

void screen_scan_show_status(const char *msg)
{
    lv_label_set_text(s_lbl_status_tag, msg);
    if (s_status_timer) lv_timer_del(s_status_timer);
    s_status_timer = lv_timer_create(clear_status_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_status_timer, 1);
}

void screen_scan_flash(bool duplicate)
{
    lv_color_t color = duplicate
        ? lv_palette_main(LV_PALETTE_RED)
        : lv_palette_main(LV_PALETTE_GREEN);
    lv_obj_set_style_bg_color(s_flash_overlay, color, LV_PART_MAIN);
    lv_obj_clear_flag(s_flash_overlay, LV_OBJ_FLAG_HIDDEN);
    if (s_flash_timer) lv_timer_del(s_flash_timer);
    s_flash_timer = lv_timer_create(hide_flash_cb, 600, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

void screen_scan_refresh_language(void)
{
    lv_label_set_text(s_lbl_no_session,      i18n_t(STR_SCAN_NO_SESSION));
    lv_label_set_text(s_lbl_btn_go_sessions, i18n_t(STR_SESSION_TITLE));
    lv_textarea_set_placeholder_text(s_ta_note, i18n_t(STR_NOTE_PLACEHOLDER));
    lv_label_set_text(s_lbl_weight_title,    i18n_t(STR_WEIGHT_KG));
    lv_label_set_text(s_lbl_preg_title,      i18n_t(STR_PREG_RESULT));
    lv_label_set_text(s_lbl_tb_title,        i18n_t(STR_TB_RESULT));
    lv_label_set_text(s_lbl_vax_title,       i18n_t(STR_SESSION_SELECT_VAX));

    lv_label_set_text(s_lbl_preg[0], i18n_t(STR_PREG_UNKNOWN));
    lv_label_set_text(s_lbl_preg[1], i18n_t(STR_PREG_YES));
    lv_label_set_text(s_lbl_preg[2], i18n_t(STR_PREG_NO));

    lv_label_set_text(s_lbl_tb[0], i18n_t(STR_TB_INCONCLUSIVE));
    lv_label_set_text(s_lbl_tb[1], i18n_t(STR_TB_POSITIVE));
    lv_label_set_text(s_lbl_tb[2], i18n_t(STR_TB_NEGATIVE));

    update_session_bar();  // refreshes session name + count

    if (!s_eid_pending) {
        set_status_ready();
    }
}
