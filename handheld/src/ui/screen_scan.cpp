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
#include "fonts.h"

// ── Layout constants ──────────────────────────────────────────────────────────
// 480 × 320 landscape
//
//  y=  0, h=60  Header — 2 rows, back button spans full height
//               row 1 (h=30): session name centred
//               row 2 (h=30): clock left | tag count right | pencil (animal note) right
//  y= 60, h=58  EID area — 2 centred rows
//               row 1 (h=30): EID value (montserrat_26)
//               row 2 (h=28): status badge (new animal / already scanned / ready)
//  y=118, h=196 Data panel  (type-specific widgets, extended to bottom of screen)
//
//  Animal note: accessed via pencil button in header → full-screen modal with keyboard.
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
static test_result_t   s_test_result  = TEST_INCONCLUSIVE;

// ── Screen root ───────────────────────────────────────────────────────────────
static lv_obj_t *s_scr = NULL;

// ── Header ────────────────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_sess_name;   // session name (row 1, centred)
static lv_obj_t *s_lbl_clock;       // date/time (row 2, left)
static lv_obj_t *s_lbl_hdr_count;   // tag count (row 2, right)

// ── EID area ──────────────────────────────────────────────────────────────────
static lv_obj_t *s_eid_row;
static lv_obj_t *s_lbl_eid;
static lv_obj_t *s_lbl_status_tag;  // "New animal" / "Already scanned" / "Ready to scan"
static lv_obj_t *s_lbl_vax_compact; // single-line vaccine list (no-input vax sessions only)

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
static lv_obj_t *s_btn_preg[6];       // 6 pregnancy outcomes (display order)
static lv_obj_t *s_lbl_preg[6];

// Display order: Unknown, Not pregnant, Rejected | Small, Medium, Big
static const pregnancy_result_t s_preg_btn_val[6] = {
    PREGNANCY_UNKNOWN, PREGNANCY_NO, PREGNANCY_REJECTED,
    PREGNANCY_SMALL,   PREGNANCY_MEDIUM, PREGNANCY_BIG,
};

static lv_obj_t *s_panel_test;
static lv_obj_t *s_lbl_test_title;    // shows the test name from session meta
static lv_obj_t *s_btn_test[3];       // TEST_INCONCLUSIVE, POSITIVE, NEGATIVE
static lv_obj_t *s_lbl_test[3];

static lv_obj_t *s_panel_vax;
static lv_obj_t *s_lbl_vax_title;
static lv_obj_t *s_lbl_vax_list;      // comma-separated vaccine names

// ── Animal note — state + modal ───────────────────────────────────────────────
static char      s_animal_note[SESSION_NOTE_MAX] = {0};  // current per-animal note
static lv_obj_t *s_btn_sess_note;         // pencil button in header (opens animal note)
static lv_obj_t *s_animal_note_overlay;   // full-screen modal container
static lv_obj_t *s_ta_animal_note;        // textarea inside modal
static lv_obj_t *s_kb_animal_note;        // keyboard inside modal
static bool      s_animal_note_active = false;

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

static lv_obj_t *s_hdr = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────
static void clock_tick_cb(lv_timer_t *t);
static void hide_flash_cb(lv_timer_t *t);
static void clear_status_cb(lv_timer_t *t);
static void show_data_panel_for_type(uint8_t type);
static void update_weight_label(void);
static void update_preg_buttons(void);
static void update_test_buttons(void);
static void open_animal_note_modal(void);
static void close_animal_note_modal(void);

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
    for (int i = 0; i < 6; i++) {
        bool sel = (s_preg == s_preg_btn_val[i]);
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

static void update_test_buttons(void)
{
    for (int i = 0; i < 3; i++) {
        bool sel = (s_test_result == (test_result_t)i);
        lv_obj_set_style_bg_color(s_btn_test[i],
            sel ? lv_palette_main(LV_PALETTE_BLUE)
                : lv_palette_lighten(LV_PALETTE_BLUE, 4),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_btn_test[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_lbl_test[i],
            sel ? lv_color_white() : lv_color_black(),
            LV_PART_MAIN);
    }
}

static void show_data_panel_for_type(uint8_t type)
{
    // Hide everything first
    lv_obj_add_flag(s_panel_none,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_weighing,  LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_preg,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_test,      LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_vax,       LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_vax_compact, LV_OBJ_FLAG_HIDDEN);

    bool is_input = (type == SESSION_TYPE_WEIGHING  ||
                     type == SESSION_TYPE_PREGNANCY  ||
                     type == SESSION_TYPE_TEST);

    if (is_input) {
        // EID area: y=60 h=70 — more vertical gap between EID value and badge
        lv_obj_set_size(s_eid_row, 480, 70);
        lv_obj_set_pos (s_eid_row, 0, 60);
        lv_obj_align(s_lbl_eid,        LV_ALIGN_TOP_MID,    0,  6);
        lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -6);

        // Data panel: y=130 h=184, just below EID area, extends to y=314 (6px margin)
        lv_obj_set_size(s_data_panel, 480, 184);
        lv_obj_set_pos (s_data_panel, 0, 130);
        lv_obj_clear_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);

        switch ((session_type_t)type) {
            case SESSION_TYPE_WEIGHING:  lv_obj_clear_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN); break;
            case SESSION_TYPE_PREGNANCY: lv_obj_clear_flag(s_panel_preg,     LV_OBJ_FLAG_HIDDEN); break;
            case SESSION_TYPE_TEST:      lv_obj_clear_flag(s_panel_test,     LV_OBJ_FLAG_HIDDEN); break;
            default: break;
        }
    } else if (type == SESSION_TYPE_VACCINATION) {
        // EID+badge centred in y=60..270 space (210px), shifted up to leave
        // room for the compact vaccine line below.
        // Group: EID-row(90) + gap(12) + vax-line(28) = 130px
        // Top: 60 + (210-130)/2 = 100
        lv_obj_set_size(s_eid_row, 480, 90);
        lv_obj_set_pos (s_eid_row, 0, 100);
        lv_obj_align(s_lbl_eid,        LV_ALIGN_TOP_MID,    0, 10);
        lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -10);

        lv_obj_add_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lbl_vax_compact, LV_OBJ_FLAG_HIDDEN);
        // Compact vax label sits 12px below the EID row (y=100+90+12=202)
        lv_obj_set_pos(s_lbl_vax_compact, 0, 202);
    } else {
        // General / Removal: EID+badge centred vertically in y=60..270 (210px)
        // EID-row height 90px → top = 60 + (210-90)/2 = 120
        lv_obj_set_size(s_eid_row, 480, 90);
        lv_obj_set_pos (s_eid_row, 0, 120);
        lv_obj_align(s_lbl_eid,        LV_ALIGN_TOP_MID,    0, 10);
        lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -10);

        lv_obj_add_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

// Fill the vaccination panel with vaccine names from the current session.
static void populate_vax_panel(void)
{
    if (!s_has_session || s_session.vax_count == 0) {
        lv_label_set_text(s_lbl_vax_list, "—");
        lv_label_set_text(s_lbl_vax_compact, "—");
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
    const char *text = buf[0] ? buf : "—";
    lv_label_set_text(s_lbl_vax_list, text);
    lv_label_set_text(s_lbl_vax_compact, text);
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
    for (int i = 0; i < 6; i++) {
        if (enabled) lv_obj_clear_state(s_btn_preg[i], LV_STATE_DISABLED);
        else         lv_obj_add_state  (s_btn_preg[i], LV_STATE_DISABLED);
    }

    // Test buttons
    for (int i = 0; i < 3; i++) {
        if (enabled) lv_obj_clear_state(s_btn_test[i], LV_STATE_DISABLED);
        else         lv_obj_add_state  (s_btn_test[i], LV_STATE_DISABLED);
    }

    // Animal note pencil button
    if (enabled) lv_obj_clear_state(s_btn_sess_note, LV_STATE_DISABLED);
    else         lv_obj_add_state  (s_btn_sess_note, LV_STATE_DISABLED);

    (void)state;
}

static void open_animal_note_modal(void)
{
    if (s_animal_note_active || !s_has_session || !s_eid_pending) return;
    s_animal_note_active = true;
    lv_textarea_set_text(s_ta_animal_note, s_animal_note);
    lv_keyboard_set_textarea(s_kb_animal_note, s_ta_animal_note);
    lv_obj_clear_flag(s_animal_note_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(s_ta_animal_note, 0, LV_ANIM_OFF);
}

static void close_animal_note_modal(void)
{
    if (!s_animal_note_active) return;
    s_animal_note_active = false;
    const char *text = lv_textarea_get_text(s_ta_animal_note);
    if (text) strlcpy(s_animal_note, text, sizeof(s_animal_note));
    lv_obj_add_flag(s_animal_note_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb_animal_note, NULL);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e)
{
    (void)e;
    close_animal_note_modal();
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

static void on_test_btn(lv_event_t *e)
{
    s_test_result = (test_result_t)(intptr_t)lv_event_get_user_data(e);
    update_test_buttons();
}

static void on_animal_note_btn(lv_event_t *e)
{
    (void)e;
    open_animal_note_modal();
}

static void on_animal_note_done(lv_event_t *e)
{
    (void)e;
    close_animal_note_modal();
}

static void on_animal_kb_ready(lv_event_t *e)
{
    (void)e;
    close_animal_note_modal();
}

static void on_animal_ta_focused(lv_event_t *e)
{
    (void)e;
    lv_keyboard_set_textarea(s_kb_animal_note, s_ta_animal_note);
    lv_obj_clear_flag(s_kb_animal_note, LV_OBJ_FLAG_HIDDEN);
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;
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
    lv_obj_set_style_text_font(lbl, &pilocows_font_18, LV_PART_MAIN);
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
    lv_obj_set_style_text_font(lbl_back, &pilocows_font_22, LV_PART_MAIN);
    lv_obj_center(lbl_back);

    // Row 1: session name — centred between back button and note button
    s_lbl_sess_name = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_sess_name, "");
    lv_label_set_long_mode(s_lbl_sess_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_sess_name, 362);   // 56..418 — leaves room for note button
    lv_obj_set_style_text_font(s_lbl_sess_name, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_sess_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_sess_name, 56, 6);

    // Row 2: clock (left) + tag count (right, before note button)
    s_lbl_clock = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_clock, "-- --- --:--");
    lv_obj_set_style_text_font(s_lbl_clock, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_clock, 58, 36);

    s_lbl_hdr_count = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_hdr_count, "0");
    lv_obj_set_width(s_lbl_hdr_count, 80);
    lv_obj_set_style_text_font(s_lbl_hdr_count, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_hdr_count, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_hdr_count, 336, 36);   // ends at 416, before note button

    // Note (pencil) button — top-right, mirrors the back button dimensions
    s_btn_sess_note = lv_btn_create(s_hdr);
    lv_obj_set_size(s_btn_sess_note, 52, 56);
    lv_obj_align(s_btn_sess_note, LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_set_style_border_width(s_btn_sess_note, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_sess_note, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_sess_note, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_sess_note, 6);
    lv_obj_add_event_cb(s_btn_sess_note, on_animal_note_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_note_icon = lv_label_create(s_btn_sess_note);
    lv_label_set_text(lbl_note_icon, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_font(lbl_note_icon, &pilocows_font_22, LV_PART_MAIN);
    lv_obj_center(lbl_note_icon);
    lv_obj_add_flag(s_btn_sess_note, LV_OBJ_FLAG_HIDDEN);  // hidden until session set

    // ── EID area — size/position set dynamically in show_data_panel_for_type() ──
    // Default matches input-session layout (y=60 h=70); overridden on first
    // call to screen_scan_set_session().
    s_eid_row = lv_obj_create(s_scr);
    lv_obj_set_size(s_eid_row, 480, 70);
    lv_obj_set_pos(s_eid_row, 0, 60);
    lv_obj_clear_flag(s_eid_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_eid_row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_eid_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_eid_row, 0, LV_PART_MAIN);

    // Row 1: EID value — centred, larger font
    s_lbl_eid = lv_label_create(s_eid_row);
    lv_label_set_text(s_lbl_eid, "---");
    lv_obj_set_style_text_font(s_lbl_eid, &pilocows_font_26, LV_PART_MAIN);
    lv_obj_align(s_lbl_eid, LV_ALIGN_TOP_MID, 0, 6);

    // Row 2: status badge — centred
    s_lbl_status_tag = lv_label_create(s_eid_row);
    lv_obj_set_style_text_font(s_lbl_status_tag, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_style_radius(s_lbl_status_tag, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_lbl_status_tag, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_lbl_status_tag, 3, LV_PART_MAIN);
    lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -6);
    set_status_ready();

    // ── Data panel (y=118 h=202) ──────────────────────────────────────────────
    // Fills to screen bottom (y=320). Note input uses a full-screen overlay so
    // it does not need reserved space here. Growing the panel removes the y=270
    // clip that was blocking ext_click_area on child button bottoms.
    s_data_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_data_panel, 480, 202);
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
    lv_obj_set_style_text_font(s_lbl_hint, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_hint, LV_ALIGN_CENTER, 0, 0);

    // ── Panel: Weighing ────────────────────────────────────────────────────────
    // Layout: title at y=0, then 7 controls across 464px at y=26:
    //   buttons h=134 fills to y=26+134=160 (168 inner − 8 slack)
    s_panel_weighing = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_weighing, 464, 168);
    lv_obj_set_pos(s_panel_weighing, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_weighing, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_weighing, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_weighing, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_weighing, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_weight_title = lv_label_create(s_panel_weighing);
    lv_label_set_text(s_lbl_weight_title, i18n_t(STR_WEIGHT_KG));
    lv_obj_set_style_text_font(s_lbl_weight_title, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_weight_title, 0, 0);

    // Button row: -100 | -10 | -1 | [value] | +1 | +10 | +100
    // Each button 62px wide, val_box 68px, 4px gaps → 7×62+68-62+6×4 = 464px total

    // -100 button
    s_btn_w_minus100 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus100, 62, 134);
    lv_obj_set_pos(s_btn_w_minus100, 0, 26);
    lv_obj_set_style_radius(s_btn_w_minus100, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus100, 10);
    lv_obj_add_event_cb(s_btn_w_minus100, on_w_minus100, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_m100 = lv_label_create(s_btn_w_minus100);
    lv_label_set_text(lbl_m100, "-100");
    lv_obj_set_style_text_font(lbl_m100, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(lbl_m100);

    // -10 button
    s_btn_w_minus10 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus10, 62, 134);
    lv_obj_set_pos(s_btn_w_minus10, 66, 26);
    lv_obj_set_style_radius(s_btn_w_minus10, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus10, 10);
    lv_obj_add_event_cb(s_btn_w_minus10, on_w_minus_long, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_m10 = lv_label_create(s_btn_w_minus10);
    lv_label_set_text(lbl_m10, "-10");
    lv_obj_set_style_text_font(lbl_m10, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(lbl_m10);

    // -1 button
    s_btn_w_minus = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus, 62, 134);
    lv_obj_set_pos(s_btn_w_minus, 132, 26);
    lv_obj_set_style_radius(s_btn_w_minus, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus, 10);
    lv_obj_add_event_cb(s_btn_w_minus, on_w_minus, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_minus = lv_label_create(s_btn_w_minus);
    lv_label_set_text(lbl_minus, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_font(lbl_minus, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(lbl_minus);

    // Value display
    lv_obj_t *val_box = lv_obj_create(s_panel_weighing);
    lv_obj_set_size(val_box, 68, 134);
    lv_obj_set_pos(val_box, 198, 26);
    lv_obj_set_style_border_width(val_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(val_box, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(val_box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(val_box, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_weight_val = lv_label_create(val_box);
    lv_label_set_text(s_lbl_weight_val, "0 kg");
    lv_obj_set_style_text_font(s_lbl_weight_val, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_weight_val, LV_ALIGN_CENTER, 0, 0);

    // +1 button
    s_btn_w_plus = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus, 62, 134);
    lv_obj_set_pos(s_btn_w_plus, 270, 26);
    lv_obj_set_style_radius(s_btn_w_plus, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus, 10);
    lv_obj_add_event_cb(s_btn_w_plus, on_w_plus, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_plus = lv_label_create(s_btn_w_plus);
    lv_label_set_text(lbl_plus, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_font(lbl_plus, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(lbl_plus);

    // +10 button
    s_btn_w_plus10 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus10, 62, 134);
    lv_obj_set_pos(s_btn_w_plus10, 336, 26);
    lv_obj_set_style_radius(s_btn_w_plus10, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus10, 10);
    lv_obj_add_event_cb(s_btn_w_plus10, on_w_plus_long, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_p10 = lv_label_create(s_btn_w_plus10);
    lv_label_set_text(lbl_p10, "+10");
    lv_obj_set_style_text_font(lbl_p10, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(lbl_p10);

    // +100 button
    s_btn_w_plus100 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus100, 62, 134);
    lv_obj_set_pos(s_btn_w_plus100, 402, 26);
    lv_obj_set_style_radius(s_btn_w_plus100, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus100, 10);
    lv_obj_add_event_cb(s_btn_w_plus100, on_w_plus100, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_p100 = lv_label_create(s_btn_w_plus100);
    lv_label_set_text(lbl_p100, "+100");
    lv_obj_set_style_text_font(lbl_p100, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(lbl_p100);

    // ── Panel: Pregnancy ───────────────────────────────────────────────────────
    // 2 rows × 3 buttons filling the full 168px panel height (no title label).
    // 3 × 150px with 7px gaps = 464px wide ✓
    // Row 1 (y=0,  h=80): Unknown | Not pregnant | Rejected
    // Row 2 (y=88, h=80): Small   | Medium       | Big
    s_panel_preg = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_preg, 464, 168);
    lv_obj_set_pos(s_panel_preg, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_preg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_preg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_preg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_preg, LV_OBJ_FLAG_SCROLLABLE);

    static const char * const s_preg_str[6] = {
        STR_PREG_UNKNOWN, STR_PREG_NO, STR_PREG_REJECTED,
        STR_PREG_SMALL,   STR_PREG_MEDIUM, STR_PREG_BIG,
    };
    for (int i = 0; i < 6; i++) {
        int col = i % 3;
        int row = i / 3;
        int x   = col * 157;
        int y   = row * 88;
        s_btn_preg[i] = make_radio_btn(s_panel_preg,
            x, y, 150, 80,
            i18n_t(s_preg_str[i]), on_preg_btn,
            (void *)(intptr_t)s_preg_btn_val[i],
            &s_lbl_preg[i]);
    }
    update_preg_buttons();

    // ── Panel: Test ────────────────────────────────────────────────────────────
    // Title (test name) at top, 3 buttons below filling remaining height.
    // Group: title(22) + gap(10) + buttons(90) = 122 → top margin = (168-122)/2 = 23
    s_panel_test = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_test, 464, 168);
    lv_obj_set_pos(s_panel_test, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_test, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_test, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_test, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_test, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_test_title = lv_label_create(s_panel_test);
    lv_label_set_text(s_lbl_test_title, i18n_t(STR_TEST_RESULT));
    lv_obj_set_style_text_font(s_lbl_test_title, &pilocows_font_18, LV_PART_MAIN);
    lv_label_set_long_mode(s_lbl_test_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_test_title, 464);
    lv_obj_set_pos(s_lbl_test_title, 0, 23);

    const char *test_labels[] = {
        i18n_t(STR_TEST_INCONCLUSIVE), i18n_t(STR_TEST_POSITIVE), i18n_t(STR_TEST_NEGATIVE)
    };
    for (int i = 0; i < 3; i++) {
        s_btn_test[i] = make_radio_btn(s_panel_test,
            4 + i * 154, 55, 146, 90,
            test_labels[i], on_test_btn, (void *)(intptr_t)i,
            &s_lbl_test[i]);
    }
    update_test_buttons();

    // ── Panel: Vaccination ────────────────────────────────────────────────────
    s_panel_vax = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_vax, 464, 76);
    lv_obj_set_pos(s_panel_vax, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_vax, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_vax, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_vax, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_vax_title = lv_label_create(s_panel_vax);
    lv_label_set_text(s_lbl_vax_title, i18n_t(STR_SESSION_SELECT_VAX));
    lv_obj_set_style_text_font(s_lbl_vax_title, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_title, 0, 0);

    s_lbl_vax_list = lv_label_create(s_panel_vax);
    lv_label_set_text(s_lbl_vax_list, "—");
    lv_label_set_long_mode(s_lbl_vax_list, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_vax_list, 460);
    lv_obj_set_style_text_font(s_lbl_vax_list, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_list, 0, 24);

    // Initial state: all hidden until session is set
    lv_obj_add_flag(s_panel_none,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_preg,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_test,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_vax,      LV_OBJ_FLAG_HIDDEN);

    // ── Compact vaccine label (Vaccination sessions, no-input layout) ─────────
    // Position is set dynamically in show_data_panel_for_type(); hidden until needed.
    s_lbl_vax_compact = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_vax_compact, "—");
    lv_label_set_long_mode(s_lbl_vax_compact, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_vax_compact, 460);
    lv_obj_set_style_text_font(s_lbl_vax_compact, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_vax_compact, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_compact, 10, 202);
    lv_obj_add_flag(s_lbl_vax_compact, LV_OBJ_FLAG_HIDDEN);

    // ── No-session overlay ────────────────────────────────────────────────────
    s_no_session_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_no_session_panel, 480, 260);
    lv_obj_set_pos(s_no_session_panel, 0, 60);
    lv_obj_clear_flag(s_no_session_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_no_session_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_no_session_panel, 0, LV_PART_MAIN);

    s_lbl_no_session = lv_label_create(s_no_session_panel);
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SCAN_NO_SESSION));
    lv_obj_set_style_text_font(s_lbl_no_session, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_no_session, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *btn_go = lv_btn_create(s_no_session_panel);
    lv_obj_set_size(btn_go, 240, 44);
    lv_obj_align(btn_go, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_radius(btn_go, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_go, 10);
    lv_obj_add_event_cb(btn_go, on_go_sessions, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_go_sessions = lv_label_create(btn_go);
    lv_label_set_text(s_lbl_btn_go_sessions, i18n_t(STR_SESSION_TITLE));
    lv_obj_set_style_text_font(s_lbl_btn_go_sessions, &pilocows_font_20, LV_PART_MAIN);
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

    // ── Animal note modal (full-screen overlay, z-order top) ──────────────────
    // Layout (320px tall):
    //   y=  0..50  header row: "Animal note" label + Done button
    //   y= 54..119  textarea (65px, ~3 lines of text)
    //   y=120..320  keyboard (200px)
    s_animal_note_overlay = lv_obj_create(s_scr);
    lv_obj_set_size(s_animal_note_overlay, 480, 320);
    lv_obj_set_pos(s_animal_note_overlay, 0, 0);
    lv_obj_clear_flag(s_animal_note_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(s_animal_note_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_animal_note_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_animal_note_overlay, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_animal_note_overlay, LV_OBJ_FLAG_HIDDEN);

    // "Note" title label
    lv_obj_t *lbl_an_title = lv_label_create(s_animal_note_overlay);
    lv_label_set_text(lbl_an_title, i18n_t(STR_NOTE));
    lv_obj_set_style_text_font(lbl_an_title, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_set_pos(lbl_an_title, 12, 14);

    // Done button
    lv_obj_t *btn_an_done = lv_btn_create(s_animal_note_overlay);
    lv_obj_set_size(btn_an_done, 100, 40);
    lv_obj_set_pos(btn_an_done, 372, 5);
    lv_obj_set_style_radius(btn_an_done, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_an_done, 8);
    lv_obj_add_event_cb(btn_an_done, on_animal_note_done, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_an_done = lv_label_create(btn_an_done);
    lv_label_set_text(lbl_an_done, i18n_t(STR_BTN_OK));
    lv_obj_set_style_text_font(lbl_an_done, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_center(lbl_an_done);

    // Textarea
    s_ta_animal_note = lv_textarea_create(s_animal_note_overlay);
    lv_obj_set_size(s_ta_animal_note, 464, 65);
    lv_obj_set_pos(s_ta_animal_note, 8, 54);
    lv_textarea_set_one_line(s_ta_animal_note, false);
    lv_textarea_set_max_length(s_ta_animal_note, SESSION_NOTE_MAX - 1);
    lv_textarea_set_placeholder_text(s_ta_animal_note, i18n_t(STR_NOTE_PLACEHOLDER));
    lv_obj_set_scrollbar_mode(s_ta_animal_note, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_text_font(s_ta_animal_note, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ta_animal_note, on_animal_ta_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_animal_note, on_animal_ta_focused, LV_EVENT_CLICKED, NULL);

    // Keyboard
    s_kb_animal_note = lv_keyboard_create(s_animal_note_overlay);
    lv_obj_set_size(s_kb_animal_note, 480, 200);
    lv_obj_align(s_kb_animal_note, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb_animal_note, &pilocows_font_22, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb_animal_note, on_animal_kb_ready, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb_animal_note, on_animal_kb_ready, LV_EVENT_CANCEL, NULL);

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
        lv_obj_clear_flag(s_btn_sess_note, LV_OBJ_FLAG_HIDDEN);
        // show_data_panel_for_type() handles data panel + eid_row visibility/position
        show_data_panel_for_type(meta->type);
        populate_vax_panel();
        // For Test sessions, set the panel title to the configured test name
        if (meta->type == SESSION_TYPE_TEST) {
            char tname[TEST_NAME_MAX] = "";
            if (meta->test_id != 0) {
                test_get_name(meta->test_id, tname, sizeof(tname));
            }
            lv_label_set_text(s_lbl_test_title, tname[0] ? tname : i18n_t(STR_TEST_RESULT));
        }
    } else {
        s_has_session = false;
        close_animal_note_modal();
        lv_obj_clear_flag(s_no_session_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_data_panel,       LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_vax_compact,  LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_btn_sess_note,    LV_OBJ_FLAG_HIDDEN);
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
            strlcpy(s_animal_note, rec.note, sizeof(s_animal_note));

            uint8_t type = s_session.type;
            if (type == SESSION_TYPE_WEIGHING) {
                uint16_t w;
                memcpy(&w, rec.data, sizeof(w));
                s_weight_kg = w;
                update_weight_label();
            } else if (type == SESSION_TYPE_PREGNANCY) {
                s_preg = (pregnancy_result_t)rec.data[0];
                update_preg_buttons();
            } else if (type == SESSION_TYPE_TEST) {
                s_test_result = (test_result_t)rec.data[0];
                update_test_buttons();
            }
        }
    } else {
        // New tag — reset data fields, keep last weight for weighing
        s_animal_note[0] = '\0';
        if (s_has_session) {
            uint8_t type = s_session.type;
            if (type == SESSION_TYPE_PREGNANCY) {
                s_preg = PREGNANCY_UNKNOWN;
                update_preg_buttons();
            } else if (type == SESSION_TYPE_TEST) {
                s_test_result = TEST_INCONCLUSIVE;
                update_test_buttons();
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
    strlcpy(out->note, s_animal_note, sizeof(out->note));

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
        case SESSION_TYPE_TEST: {
            out->data[0] = (uint8_t)s_test_result;
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
    s_animal_note[0] = '\0';
    // Reset data fields
    s_preg        = PREGNANCY_UNKNOWN;
    s_test_result = TEST_INCONCLUSIVE;
    update_preg_buttons();
    update_test_buttons();
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
    lv_textarea_set_placeholder_text(s_ta_animal_note, i18n_t(STR_NOTE_PLACEHOLDER));
    lv_label_set_text(s_lbl_weight_title,    i18n_t(STR_WEIGHT_KG));
    // Test title is set dynamically in screen_scan_set_session; skip here to
    // avoid overwriting the configured test name with the generic fallback.
    lv_label_set_text(s_lbl_vax_title,       i18n_t(STR_SESSION_SELECT_VAX));

    // Display order matches s_preg_btn_val[]: Unknown, No, Rejected, Small, Medium, Big
    lv_label_set_text(s_lbl_preg[0], i18n_t(STR_PREG_UNKNOWN));
    lv_label_set_text(s_lbl_preg[1], i18n_t(STR_PREG_NO));
    lv_label_set_text(s_lbl_preg[2], i18n_t(STR_PREG_REJECTED));
    lv_label_set_text(s_lbl_preg[3], i18n_t(STR_PREG_SMALL));
    lv_label_set_text(s_lbl_preg[4], i18n_t(STR_PREG_MEDIUM));
    lv_label_set_text(s_lbl_preg[5], i18n_t(STR_PREG_BIG));

    lv_label_set_text(s_lbl_test[0], i18n_t(STR_TEST_INCONCLUSIVE));
    lv_label_set_text(s_lbl_test[1], i18n_t(STR_TEST_POSITIVE));
    lv_label_set_text(s_lbl_test[2], i18n_t(STR_TEST_NEGATIVE));

    update_session_bar();  // refreshes session name + count

    if (!s_eid_pending) {
        set_status_ready();
    }
}
