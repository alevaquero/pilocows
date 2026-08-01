#include "screen_scan.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "ui_keyboard.h"
#include "ui_text_entry.h"
#include "screen_audio_note.h"
#include "i18n.h"
#include "strings_en.h"
#include "session_storage.h"
#include "esp_log.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>

static const char *TAG_UNUSED = "screen_scan"; // kept for parity with logging convention elsewhere

// ── Layout ────────────────────────────────────────────────────────────────────
// 480x800 portrait:
//
//  y=  0, h=80   Header — back button (left) | date/time (centred) | pencil (right)
//  y= 84, h=38   Session name (single line, truncated, same font as main menu card)
//  y=126, h=25   "Scanned: N" (same look as the main menu's session card)
//  y=160, h~105+ EID area — 2 centred rows (position/size depends on session type)
//  Data panel: type-specific widgets, below EID area, stacked vertically to fit
//              the 480px width (weighing/pregnancy/test sub-panels use multiple rows).
//              Sub-panels are bottom-aligned within it so the actual buttons sit
//              near the bottom of the screen, closer to the user's thumb.
//
//  Animal note: accessed via pencil button in header -> full-screen modal with keyboard.
//  Flash overlay: full 480x800, z-order top, hidden by default.

// ── Internal state ───────────────────────────────────────────────────────────
static session_meta_t s_session;
static bool s_has_session = false;
static char s_current_eid[SESSION_EID_MAX + 1] = {0};
static bool s_eid_pending = false;

// Type-specific state
static uint16_t s_weight_kg = 0;
static uint32_t s_weight_session_id = 0;
static pregnancy_result_t s_preg = PREGNANCY_UNKNOWN;
static test_result_t s_test_result = TEST_INCONCLUSIVE;

#define ANIMAL_NOTE_MAX 256
static char s_animal_note[ANIMAL_NOTE_MAX] = {0};

// Pending tag's voice note. Unlike the text note, audio is written straight
// to SD as soon as it's recorded (an active session already exists here, so
// there's no need to stage it in RAM the way screen_session_new.c must) —
// these two fields just track what to reflect in tag_record_t at commit time
// (screen_scan_get_record()) and which file to overwrite/play/delete.
static bool s_tag_has_audio = false;
static uint16_t s_tag_audio_seq = 0;

// ── Screen root ──────────────────────────────────────────────────────────────
static lv_obj_t *s_scr = NULL;

// ── Header ───────────────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_sess_name;
static lv_obj_t *s_lbl_scan_count;
static lv_obj_t *s_lbl_clock;
static lv_obj_t *s_btn_sess_note;
static lv_obj_t *s_btn_tag_audio;

// ── EID area ─────────────────────────────────────────────────────────────────
static lv_obj_t *s_eid_row;
static lv_obj_t *s_lbl_eid;
static lv_obj_t *s_lbl_status_tag;
static lv_obj_t *s_lbl_vax_compact;

// ── Data panel and sub-panels ────────────────────────────────────────────────
static lv_obj_t *s_data_panel;
static lv_obj_t *s_panel_none;
static lv_obj_t *s_lbl_hint;

static lv_obj_t *s_panel_weighing;
static lv_obj_t *s_lbl_weight_val;
static lv_obj_t *s_btn_w_minus100, *s_btn_w_minus10, *s_btn_w_minus;
static lv_obj_t *s_btn_w_plus, *s_btn_w_plus10, *s_btn_w_plus100;

static lv_obj_t *s_panel_preg;
static lv_obj_t *s_btn_preg[6];
static lv_obj_t *s_lbl_preg[6];
static const pregnancy_result_t s_preg_btn_val[6] = {
    PREGNANCY_UNKNOWN, PREGNANCY_NO, PREGNANCY_REJECTED,
    PREGNANCY_SMALL, PREGNANCY_MEDIUM, PREGNANCY_BIG,
};

static lv_obj_t *s_panel_test;
static lv_obj_t *s_lbl_test_title;
static lv_obj_t *s_btn_test[3];
static lv_obj_t *s_lbl_test[3];
static const test_result_t s_test_btn_val[3] = {
    TEST_INCONCLUSIVE, TEST_POSITIVE, TEST_NEGATIVE,
};

static lv_obj_t *s_panel_vax;
static lv_obj_t *s_lbl_vax_title;
static lv_obj_t *s_lbl_vax_list;

// ── No-session overlay ───────────────────────────────────────────────────────
static lv_obj_t *s_no_session_panel;
static lv_obj_t *s_lbl_no_session;
static lv_obj_t *s_lbl_btn_go_sessions;

// ── Flash overlay ────────────────────────────────────────────────────────────
static lv_obj_t *s_flash_overlay;

// ── Timers ───────────────────────────────────────────────────────────────────
static lv_timer_t *s_flash_timer = NULL;
static lv_timer_t *s_clock_timer = NULL;
static lv_timer_t *s_status_timer = NULL;

// ── Forward declarations ─────────────────────────────────────────────────────
static void show_data_panel_for_type(uint8_t type);
static void update_weight_label(void);
static void update_preg_buttons(void);
static void update_test_buttons(void);
static void set_status_ready(void);

// ── Helpers ──────────────────────────────────────────────────────────────────

static void update_session_bar(void) {
    if (!s_has_session) {
        lv_label_set_text(s_lbl_sess_name, i18n_t(STR_SCAN_NO_SESSION));
        lv_label_set_text(s_lbl_scan_count, "");
        return;
    }
    lv_label_set_text(s_lbl_sess_name, s_session.name);
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%s %" PRIu32, i18n_t(STR_SCAN_COUNT_LABEL), s_session.tag_count);
    lv_label_set_text(s_lbl_scan_count, count_str);
}

static void update_weight_label(void) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u kg", (unsigned)s_weight_kg);
    lv_label_set_text(s_lbl_weight_val, buf);
}

static void update_preg_buttons(void) {
    for (int i = 0; i < 6; i++) {
        bool sel = (s_preg == s_preg_btn_val[i]);
        lv_obj_set_style_bg_color(s_btn_preg[i],
            sel ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_lighten(LV_PALETTE_BLUE, 4),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_btn_preg[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_lbl_preg[i], sel ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    }
}

static void update_test_buttons(void) {
    for (int i = 0; i < 3; i++) {
        bool sel = (s_test_result == s_test_btn_val[i]);
        lv_obj_set_style_bg_color(s_btn_test[i],
            sel ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_lighten(LV_PALETTE_BLUE, 4),
            LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_btn_test[i], LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(s_lbl_test[i], sel ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    }
}

static void show_data_panel_for_type(uint8_t type) {
    lv_obj_add_flag(s_panel_none, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_preg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_test, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_vax, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_vax_compact, LV_OBJ_FLAG_HIDDEN);

    bool is_input = (type == SESSION_TYPE_WEIGHING || type == SESSION_TYPE_PREGNANCY || type == SESSION_TYPE_TEST);

    if (is_input) {
        // EID area: y=160 h=105
        lv_obj_set_size(s_eid_row, 480, 105);
        lv_obj_set_pos(s_eid_row, 0, 160);
        lv_obj_align(s_lbl_eid, LV_ALIGN_TOP_MID, 0, 9);
        lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -9);

        // Data panel: y=265, tall enough to reach near the screen bottom so
        // the bottom-aligned sub-panels below land in thumb-reach.
        lv_obj_set_size(s_data_panel, 480, 520);
        lv_obj_set_pos(s_data_panel, 0, 265);
        lv_obj_clear_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);

        switch ((session_type_t)type) {
            case SESSION_TYPE_WEIGHING:  lv_obj_clear_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN); break;
            case SESSION_TYPE_PREGNANCY: lv_obj_clear_flag(s_panel_preg, LV_OBJ_FLAG_HIDDEN); break;
            case SESSION_TYPE_TEST:      lv_obj_clear_flag(s_panel_test, LV_OBJ_FLAG_HIDDEN); break;
            default: break;
        }
    } else if (type == SESSION_TYPE_VACCINATION) {
        // EID+badge, y=220 h=135
        lv_obj_set_size(s_eid_row, 480, 135);
        lv_obj_set_pos(s_eid_row, 0, 220);
        lv_obj_align(s_lbl_eid, LV_ALIGN_TOP_MID, 0, 15);
        lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -15);

        lv_obj_add_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_lbl_vax_compact, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_lbl_vax_compact, 30, 373);
    } else {
        // General / Removal, y=250 h=135
        lv_obj_set_size(s_eid_row, 480, 135);
        lv_obj_set_pos(s_eid_row, 0, 250);
        lv_obj_align(s_lbl_eid, LV_ALIGN_TOP_MID, 0, 15);
        lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -15);

        lv_obj_add_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void populate_vax_panel(void) {
    if (!s_has_session || s_session.vax_count == 0) {
        lv_label_set_text(s_lbl_vax_list, "-");
        lv_label_set_text(s_lbl_vax_compact, "-");
        return;
    }
    char buf[256] = {0};
    for (int i = 0; i < s_session.vax_count; i++) {
        char name[VACCINE_NAME_MAX];
        if (vaccine_get_name(s_session.vax_ids[i], name, sizeof(name))) {
            if (i > 0) strncat(buf, "\n", sizeof(buf) - strlen(buf) - 1);
            strncat(buf, "\xE2\x80\xA2 ", sizeof(buf) - strlen(buf) - 1); // "• " (U+2022)
            strncat(buf, name, sizeof(buf) - strlen(buf) - 1);
        }
    }
    const char *text = buf[0] ? buf : "-";
    lv_label_set_text(s_lbl_vax_list, text);
    lv_label_set_text(s_lbl_vax_compact, text);
}

static void update_controls_enabled(bool enabled) {
    lv_obj_t *w_btns[] = {
        s_btn_w_minus100, s_btn_w_minus10, s_btn_w_minus,
        s_btn_w_plus, s_btn_w_plus10, s_btn_w_plus100,
    };
    for (int i = 0; i < 6; i++) {
        if (enabled) lv_obj_clear_state(w_btns[i], LV_STATE_DISABLED);
        else lv_obj_add_state(w_btns[i], LV_STATE_DISABLED);
    }
    for (int i = 0; i < 6; i++) {
        if (enabled) lv_obj_clear_state(s_btn_preg[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s_btn_preg[i], LV_STATE_DISABLED);
    }
    for (int i = 0; i < 3; i++) {
        if (enabled) lv_obj_clear_state(s_btn_test[i], LV_STATE_DISABLED);
        else lv_obj_add_state(s_btn_test[i], LV_STATE_DISABLED);
    }
    if (enabled) lv_obj_clear_state(s_btn_sess_note, LV_STATE_DISABLED);
    else lv_obj_add_state(s_btn_sess_note, LV_STATE_DISABLED);
    if (enabled) lv_obj_clear_state(s_btn_tag_audio, LV_STATE_DISABLED);
    else lv_obj_add_state(s_btn_tag_audio, LV_STATE_DISABLED);
}

// ── Callbacks ────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG_UNUSED, "on_back: tapped (eid_pending=%d has_session=%d)", s_eid_pending, s_has_session);
    if (s_eid_pending && s_has_session) {
        tag_record_t rec;
        if (screen_scan_get_record(&rec)) {
            ESP_LOGI(TAG_UNUSED, "on_back: committing pending tag %s", rec.eid);
            session_add_tag(&rec);
            ESP_LOGI(TAG_UNUSED, "on_back: session_add_tag done");
        }
        screen_scan_clear_pending();
        ESP_LOGI(TAG_UNUSED, "on_back: clear_pending done");
    }
    ESP_LOGI(TAG_UNUSED, "on_back: calling ui_manager_show(SCREEN_SESSION_MENU)");
    ui_manager_show(SCREEN_SESSION_MENU);
    ESP_LOGI(TAG_UNUSED, "on_back: ui_manager_show returned");
}

static void on_go_sessions(lv_event_t *e) {
    (void)e;
    ui_manager_show(SCREEN_SESSION_MENU);
}

static void on_w_minus(lv_event_t *e) { (void)e; if (s_weight_kg > 0) s_weight_kg--; update_weight_label(); }
static void on_w_plus(lv_event_t *e)  { (void)e; if (s_weight_kg < 999) s_weight_kg++; update_weight_label(); }
static void on_w_minus_long(lv_event_t *e) { (void)e; s_weight_kg = (s_weight_kg >= 10) ? s_weight_kg - 10 : 0; update_weight_label(); }
static void on_w_plus_long(lv_event_t *e)  { (void)e; s_weight_kg = (s_weight_kg <= 989) ? s_weight_kg + 10 : 999; update_weight_label(); }
static void on_w_minus100(lv_event_t *e)   { (void)e; s_weight_kg = (s_weight_kg >= 100) ? s_weight_kg - 100 : 0; update_weight_label(); }
static void on_w_plus100(lv_event_t *e)    { (void)e; s_weight_kg = (s_weight_kg <= 899) ? s_weight_kg + 100 : 999; update_weight_label(); }

static void on_preg_btn(lv_event_t *e) {
    s_preg = (pregnancy_result_t)(intptr_t)lv_event_get_user_data(e);
    update_preg_buttons();
}

static void on_test_btn(lv_event_t *e) {
    s_test_result = (test_result_t)(intptr_t)lv_event_get_user_data(e);
    update_test_buttons();
}

static void on_animal_note_confirm(const char *text, void *user_data) {
    (void)user_data;
    strncpy(s_animal_note, text, sizeof(s_animal_note) - 1);
    s_animal_note[sizeof(s_animal_note) - 1] = '\0';
}

static void on_animal_note_btn(lv_event_t *e) {
    (void)e;
    if (!s_has_session || !s_eid_pending) return;
    ui_text_entry_cfg_t cfg = {
        .label = i18n_t(STR_NOTE),
        .initial_text = s_animal_note,
        .placeholder = i18n_t(STR_NOTE_PLACEHOLDER),
        .multiline = true,
        .password = false,
        .max_length = ANIMAL_NOTE_MAX - 1,
        .on_confirm = on_animal_note_confirm,
        .on_cancel = NULL,
        .user_data = NULL,
    };
    ui_text_entry_show(&cfg);
}

static void on_tag_audio_recorded(const int16_t *pcm, size_t n_samples, void *user_data) {
    (void)user_data;
    if (!s_has_session) return;
    if (session_save_tag_audio(s_tag_audio_seq, pcm, n_samples) == ESP_OK) {
        s_tag_has_audio = true;
    }
}

static void on_tag_audio_deleted(void *user_data) {
    (void)user_data;
    if (!s_has_session) return;
    char path[64];
    session_tag_audio_path(s_session.id, s_tag_audio_seq, path, sizeof(path));
    remove(path);
    s_tag_has_audio = false;
}

// screen_audio_note is a full registered screen (not an overlay), so
// navigating to it and back re-fires LV_EVENT_SCREEN_LOADED here. Without
// this flag, on_screen_loaded() can't tell "returning from the recorder"
// apart from "opening the scan screen fresh", and would run
// screen_scan_clear_pending() on return — wiping the pending tag's note,
// in-progress type-specific values, and the tag audio flags we just set.
static bool s_returning_from_recorder = false;

static void on_tag_audio_btn(lv_event_t *e) {
    (void)e;
    if (!s_has_session || !s_eid_pending) return;

    char wav_path[64] = "";
    if (s_tag_has_audio) {
        session_tag_audio_path(s_session.id, s_tag_audio_seq, wav_path, sizeof(wav_path));
    }
    audio_note_cfg_t cfg = {
        .title = i18n_t(STR_NOTE),
        .has_existing = s_tag_has_audio,
        .existing_wav_path = s_tag_has_audio ? wav_path : NULL,
        .return_screen = SCREEN_SCAN,
        .on_recorded = on_tag_audio_recorded,
        .on_deleted = on_tag_audio_deleted,
        .user_data = NULL,
    };
    s_returning_from_recorder = true;
    screen_audio_note_show(&cfg);
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    if (s_returning_from_recorder) {
        s_returning_from_recorder = false;
        ESP_LOGI(TAG_UNUSED, "on_screen_loaded: returning from recorder, preserving pending tag state");
        return; // preserve the pending tag's note/values/audio flags exactly as they were
    }
    session_meta_t m;
    if (session_get_active(&m)) {
        screen_scan_set_session(&m);
    } else {
        screen_scan_set_session(NULL);
    }
}

static void hide_flash_cb(lv_timer_t *t) {
    (void)t;
    lv_obj_add_flag(s_flash_overlay, LV_OBJ_FLAG_HIDDEN);
    s_flash_timer = NULL;
}

static void set_status_ready(void) {
    lv_label_set_text(s_lbl_status_tag, i18n_t(STR_SCAN_READY));
    lv_obj_set_style_bg_color(s_lbl_status_tag, lv_color_hex(0xFF00FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_lbl_status_tag, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status_tag, lv_color_white(), LV_PART_MAIN);
}

static void clear_status_cb(lv_timer_t *t) {
    (void)t;
    set_status_ready();
    s_status_timer = NULL;
}

static void clock_tick_cb(lv_timer_t *t) {
    (void)t;
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char buf[24];
    strftime(buf, sizeof(buf), "%d %b %H:%M", tm_info);
    lv_label_set_text(s_lbl_clock, buf);
}

// ── Helper: radio-style button ───────────────────────────────────────────────

static lv_obj_t *make_radio_btn(lv_obj_t *parent, int x, int y, int w, int h,
                                 const char *text, lv_event_cb_t cb, void *ud,
                                 lv_obj_t **lbl_out) {
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
    lv_obj_set_style_text_font(lbl, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_black(), LV_PART_MAIN);
    lv_obj_center(lbl);
    if (lbl_out) *lbl_out = lbl;

    return btn;
}

// ── Create ───────────────────────────────────────────────────────────────────

void screen_scan_create(void) {
    s_scr = lv_obj_create(NULL);
    lv_obj_set_size(s_scr, 480, 800);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header (y=0 h=80): back | date/time (centred) | pencil ─────────────
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
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_font(lbl_back, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(lbl_back);

    // Date/time only (was also carrying session name + tag count - moved out
    // to their own elements below so this row stays uncluttered).
    s_lbl_clock = lv_label_create(hdr);
    lv_label_set_text(s_lbl_clock, "-- --- --:--");
    lv_obj_set_style_text_font(s_lbl_clock, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_clock, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_clock, LV_ALIGN_CENTER, 0, 0);

    s_btn_sess_note = lv_btn_create(hdr);
    lv_obj_set_size(s_btn_sess_note, 70, 70);
    lv_obj_align(s_btn_sess_note, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_border_width(s_btn_sess_note, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_sess_note, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_sess_note, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_sess_note, 6);
    lv_obj_add_event_cb(s_btn_sess_note, on_animal_note_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_note_icon = lv_label_create(s_btn_sess_note);
    lv_label_set_text(lbl_note_icon, LV_SYMBOL_EDIT);
    lv_obj_set_style_text_font(lbl_note_icon, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(lbl_note_icon);
    lv_obj_add_flag(s_btn_sess_note, LV_OBJ_FLAG_HIDDEN);

    s_btn_tag_audio = lv_btn_create(hdr);
    lv_obj_set_size(s_btn_tag_audio, 70, 70);
    lv_obj_align_to(s_btn_tag_audio, s_btn_sess_note, LV_ALIGN_OUT_LEFT_MID, -8, 0);
    lv_obj_set_style_border_width(s_btn_tag_audio, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_tag_audio, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_btn_tag_audio, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_tag_audio, 6);
    lv_obj_add_event_cb(s_btn_tag_audio, on_tag_audio_btn, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_tag_audio_icon = lv_label_create(s_btn_tag_audio);
    lv_label_set_text(lbl_tag_audio_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(lbl_tag_audio_icon, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(lbl_tag_audio_icon);
    lv_obj_add_flag(s_btn_tag_audio, LV_OBJ_FLAG_HIDDEN);

    // ── Session name (y=84 h=38), same font as the main menu's session card ──
    s_lbl_sess_name = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_sess_name, "");
    lv_label_set_long_mode(s_lbl_sess_name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_lbl_sess_name, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_sess_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_lbl_sess_name, 480, 38);
    lv_obj_set_pos(s_lbl_sess_name, 0, 84);

    // ── Scan count (y=126 h=25), same look as the main menu's session card ──
    s_lbl_scan_count = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_scan_count, "");
    lv_obj_set_style_text_font(s_lbl_scan_count, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_scan_count, lv_palette_main(LV_PALETTE_BLUE_GREY), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_scan_count, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_size(s_lbl_scan_count, 480, 25);
    lv_obj_set_pos(s_lbl_scan_count, 0, 126);

    // ── EID area (position/size set dynamically) ──────────────────────────
    s_eid_row = lv_obj_create(s_scr);
    lv_obj_set_size(s_eid_row, 480, 105);
    lv_obj_set_pos(s_eid_row, 0, 160);
    lv_obj_clear_flag(s_eid_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_eid_row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_eid_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_eid_row, 0, LV_PART_MAIN);

    s_lbl_eid = lv_label_create(s_eid_row);
    lv_label_set_text(s_lbl_eid, "---");
    lv_obj_set_style_text_font(s_lbl_eid, &lv_font_app_36, LV_PART_MAIN);
    lv_obj_align(s_lbl_eid, LV_ALIGN_TOP_MID, 0, 9);

    s_lbl_status_tag = lv_label_create(s_eid_row);
    lv_obj_set_style_text_font(s_lbl_status_tag, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_radius(s_lbl_status_tag, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(s_lbl_status_tag, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(s_lbl_status_tag, 5, LV_PART_MAIN);
    lv_obj_align(s_lbl_status_tag, LV_ALIGN_BOTTOM_MID, 0, -9);
    set_status_ready();

    // ── Data panel (repositioned/resized per type in show_data_panel_for_type) ──
    s_data_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_data_panel, 480, 520);
    lv_obj_set_pos(s_data_panel, 0, 265);
    lv_obj_clear_flag(s_data_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_data_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_data_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_data_panel, 13, LV_PART_MAIN);

    // ── Panel: General / Removal ───────────────────────────────────────────
    s_panel_none = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_none, 454, 300);
    lv_obj_align(s_panel_none, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_none, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_none, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_none, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_hint = lv_label_create(s_panel_none);
    lv_label_set_text(s_lbl_hint, i18n_t(STR_SCAN_READY));
    lv_obj_set_style_text_font(s_lbl_hint, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_align(s_lbl_hint, LV_ALIGN_CENTER, 0, 0);

    // ── Panel: Weighing ─────────────────────────────────────────────────────
    // 480px-wide canvas can't fit all 7 widgets in one row (needed ~773px), so
    // this is stacked: title, value box, then two rows of 3 step buttons each.
    // No title label (removed) - value box sits at the top, then a +1/+10/+100
    // row, then a -1/-10/-100 row below it. Panel height is trimmed to its
    // exact content height so, once bottom-aligned in s_data_panel, the button
    // rows land flush at the bottom of the screen.
    // Panel now spans the full data-panel height: the value box sits near the
    // top (between the EID/status area and the buttons), while the +/- rows
    // stay anchored to the bottom for thumb reach.
    s_panel_weighing = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_weighing, 454, 520);
    lv_obj_align(s_panel_weighing, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_weighing, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_weighing, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_weighing, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_weighing, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *val_box = lv_obj_create(s_panel_weighing);
    lv_obj_set_size(val_box, 220, 80);
    // Vertically centered in the 240px gap between the panel top (right below
    // EID/status) and the first button row at y=240: (240-80)/2 = 80.
    lv_obj_set_pos(val_box, 117, 80);
    lv_obj_set_style_border_width(val_box, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(val_box, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(val_box, 0, LV_PART_MAIN);
    lv_obj_clear_flag(val_box, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_weight_val = lv_label_create(val_box);
    lv_label_set_text(s_lbl_weight_val, "0 kg");
    lv_obj_set_style_text_font(s_lbl_weight_val, &lv_font_app_44, LV_PART_MAIN);
    lv_obj_align(s_lbl_weight_val, LV_ALIGN_CENTER, 0, 0);

    // Row 1: +1 | +10 | +100 (y=240)   Row 2: -1 | -10 | -100 (y=390, panel
    // bottom) - each 140x130, 17px gaps
    s_btn_w_plus = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus, 140, 130);
    lv_obj_set_pos(s_btn_w_plus, 0, 240);
    lv_obj_set_style_radius(s_btn_w_plus, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus, 10);
    lv_obj_add_event_cb(s_btn_w_plus, on_w_plus, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_plus = lv_label_create(s_btn_w_plus);
    lv_label_set_text(lbl_plus, "+1");
    lv_obj_set_style_text_font(lbl_plus, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_plus);

    s_btn_w_plus10 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus10, 140, 130);
    lv_obj_set_pos(s_btn_w_plus10, 157, 240);
    lv_obj_set_style_radius(s_btn_w_plus10, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus10, 10);
    lv_obj_add_event_cb(s_btn_w_plus10, on_w_plus_long, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_p10 = lv_label_create(s_btn_w_plus10);
    lv_label_set_text(lbl_p10, "+10");
    lv_obj_set_style_text_font(lbl_p10, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_p10);

    s_btn_w_plus100 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_plus100, 140, 130);
    lv_obj_set_pos(s_btn_w_plus100, 314, 240);
    lv_obj_set_style_radius(s_btn_w_plus100, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_plus100, 10);
    lv_obj_add_event_cb(s_btn_w_plus100, on_w_plus100, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_p100 = lv_label_create(s_btn_w_plus100);
    lv_label_set_text(lbl_p100, "+100");
    lv_obj_set_style_text_font(lbl_p100, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_p100);

    s_btn_w_minus = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus, 140, 130);
    lv_obj_set_pos(s_btn_w_minus, 0, 390);
    lv_obj_set_style_radius(s_btn_w_minus, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus, 10);
    lv_obj_add_event_cb(s_btn_w_minus, on_w_minus, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_minus = lv_label_create(s_btn_w_minus);
    lv_label_set_text(lbl_minus, "-1");
    lv_obj_set_style_text_font(lbl_minus, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_minus);

    s_btn_w_minus10 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus10, 140, 130);
    lv_obj_set_pos(s_btn_w_minus10, 157, 390);
    lv_obj_set_style_radius(s_btn_w_minus10, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus10, 10);
    lv_obj_add_event_cb(s_btn_w_minus10, on_w_minus_long, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_m10 = lv_label_create(s_btn_w_minus10);
    lv_label_set_text(lbl_m10, "-10");
    lv_obj_set_style_text_font(lbl_m10, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_m10);

    s_btn_w_minus100 = lv_btn_create(s_panel_weighing);
    lv_obj_set_size(s_btn_w_minus100, 140, 130);
    lv_obj_set_pos(s_btn_w_minus100, 314, 390);
    lv_obj_set_style_radius(s_btn_w_minus100, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_w_minus100, 10);
    lv_obj_add_event_cb(s_btn_w_minus100, on_w_minus100, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_m100 = lv_label_create(s_btn_w_minus100);
    lv_label_set_text(lbl_m100, "-100");
    lv_obj_set_style_text_font(lbl_m100, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(lbl_m100);

    // ── Panel: Pregnancy ────────────────────────────────────────────────────
    // 2 columns x 3 rows (was 3x2) so it fits the 454px content width.
    s_panel_preg = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_preg, 454, 380);
    lv_obj_align(s_panel_preg, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_preg, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_preg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_preg, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_preg, LV_OBJ_FLAG_SCROLLABLE);

    static const char *const s_preg_str[6] = {
        STR_PREG_UNKNOWN, STR_PREG_NO, STR_PREG_REJECTED,
        STR_PREG_SMALL, STR_PREG_MEDIUM, STR_PREG_BIG,
    };
    for (int i = 0; i < 6; i++) {
        int col = i % 2;
        int row = i / 2;
        int x = col * 234;
        int y = row * 124;
        s_btn_preg[i] = make_radio_btn(s_panel_preg, x, y, 220, 110,
            i18n_t(s_preg_str[i]), on_preg_btn, (void *)(intptr_t)s_preg_btn_val[i], &s_lbl_preg[i]);
    }
    update_preg_buttons();

    // ── Panel: Test ─────────────────────────────────────────────────────────
    // Single stacked column (was 3-across) so it fits the 454px content width.
    s_panel_test = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_test, 454, 420);
    lv_obj_align(s_panel_test, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_test, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_test, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel_test, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_test, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_test_title = lv_label_create(s_panel_test);
    lv_label_set_text(s_lbl_test_title, i18n_t(STR_TEST_RESULT));
    lv_obj_set_style_text_font(s_lbl_test_title, &lv_font_app_28, LV_PART_MAIN);
    lv_label_set_long_mode(s_lbl_test_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_lbl_test_title, 454);
    lv_obj_set_pos(s_lbl_test_title, 0, 0);

    const char *test_labels[] = {
        i18n_t(STR_TEST_INCONCLUSIVE), i18n_t(STR_TEST_POSITIVE), i18n_t(STR_TEST_NEGATIVE)
    };
    for (int i = 0; i < 3; i++) {
        s_btn_test[i] = make_radio_btn(s_panel_test, 0, 45 + i * 125, 454, 110,
            test_labels[i], on_test_btn, (void *)(intptr_t)s_test_btn_val[i], &s_lbl_test[i]);
    }
    update_test_buttons();

    // ── Panel: Vaccination ──────────────────────────────────────────────────
    s_panel_vax = lv_obj_create(s_data_panel);
    lv_obj_set_size(s_panel_vax, 454, 200);
    lv_obj_set_pos(s_panel_vax, 0, 0);
    lv_obj_set_style_bg_opa(s_panel_vax, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel_vax, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel_vax, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_vax_title = lv_label_create(s_panel_vax);
    lv_label_set_text(s_lbl_vax_title, i18n_t(STR_SESSION_SELECT_VAX));
    lv_obj_set_style_text_font(s_lbl_vax_title, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_title, 0, 0);

    s_lbl_vax_list = lv_label_create(s_panel_vax);
    lv_label_set_text(s_lbl_vax_list, "-");
    lv_label_set_long_mode(s_lbl_vax_list, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_vax_list, 454);
    lv_obj_set_style_text_font(s_lbl_vax_list, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_list, 0, 36);

    lv_obj_add_flag(s_panel_none, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_weighing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_preg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_test, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_panel_vax, LV_OBJ_FLAG_HIDDEN);

    // ── Compact vaccine label (Vaccination sessions, no-input layout) ──────
    s_lbl_vax_compact = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_vax_compact, "-");
    lv_label_set_long_mode(s_lbl_vax_compact, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_lbl_vax_compact, 420);
    lv_obj_set_style_text_font(s_lbl_vax_compact, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_lbl_vax_compact, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(s_lbl_vax_compact, 10, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax_compact, 30, 303);
    lv_obj_add_flag(s_lbl_vax_compact, LV_OBJ_FLAG_HIDDEN);

    // ── No-session overlay ──────────────────────────────────────────────────
    s_no_session_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_no_session_panel, 480, 720);
    lv_obj_set_pos(s_no_session_panel, 0, 80);
    lv_obj_clear_flag(s_no_session_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_no_session_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_no_session_panel, 0, LV_PART_MAIN);

    s_lbl_no_session = lv_label_create(s_no_session_panel);
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SCAN_NO_SESSION));
    lv_obj_set_style_text_font(s_lbl_no_session, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_align(s_lbl_no_session, LV_ALIGN_CENTER, 0, -60);

    lv_obj_t *btn_go = lv_btn_create(s_no_session_panel);
    lv_obj_set_size(btn_go, 360, 66);
    lv_obj_align(btn_go, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_radius(btn_go, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_go, 10);
    lv_obj_add_event_cb(btn_go, on_go_sessions, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_go_sessions = lv_label_create(btn_go);
    lv_label_set_text(s_lbl_btn_go_sessions, i18n_t(STR_SESSION_TITLE));
    lv_obj_set_style_text_font(s_lbl_btn_go_sessions, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_go_sessions);

    lv_obj_add_flag(s_no_session_panel, LV_OBJ_FLAG_HIDDEN);

    // ── Flash overlay ───────────────────────────────────────────────────────
    s_flash_overlay = lv_obj_create(s_scr);
    lv_obj_set_size(s_flash_overlay, 480, 800);
    lv_obj_align(s_flash_overlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(s_flash_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_flash_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_flash_overlay, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_flash_overlay, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    lv_obj_add_flag(s_flash_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_flash_overlay, LV_OBJ_FLAG_CLICKABLE);

    // ── Clock timer ─────────────────────────────────────────────────────────
    s_clock_timer = lv_timer_create(clock_tick_cb, 1000, NULL);
    clock_tick_cb(NULL);

    (void)TAG_UNUSED;
}

// ── Public API ───────────────────────────────────────────────────────────────

void screen_scan_load(void) {
    lv_scr_load(s_scr);
    // on_screen_loaded fires via LV_EVENT_SCREEN_LOADED
}

void screen_scan_set_session(const session_meta_t *meta) {
    if (meta) {
        bool new_session = (!s_has_session || s_session.id != meta->id);
        s_session = *meta;
        s_has_session = true;
        if (new_session && meta->type == SESSION_TYPE_WEIGHING) {
            s_weight_kg = 100;
            s_weight_session_id = meta->id;
            update_weight_label();
        }
        lv_obj_add_flag(s_no_session_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_sess_note, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_btn_tag_audio, LV_OBJ_FLAG_HIDDEN);
        show_data_panel_for_type(meta->type);
        populate_vax_panel();
        if (meta->type == SESSION_TYPE_TEST) {
            char tname[TEST_NAME_MAX] = "";
            if (meta->test_id != 0) {
                test_get_name(meta->test_id, tname, sizeof(tname));
            }
            lv_label_set_text(s_lbl_test_title, tname[0] ? tname : i18n_t(STR_TEST_RESULT));
        }
    } else {
        s_has_session = false;
        lv_obj_clear_flag(s_no_session_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_data_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_lbl_vax_compact, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_btn_sess_note, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_btn_tag_audio, LV_OBJ_FLAG_HIDDEN);
    }
    update_session_bar();
    screen_scan_clear_pending();
}

void screen_scan_show_tag(const char *eid, bool is_duplicate) {
    strncpy(s_current_eid, eid, sizeof(s_current_eid) - 1);
    s_current_eid[sizeof(s_current_eid) - 1] = '\0';
    s_eid_pending = true;

    lv_label_set_text(s_lbl_eid, eid);

    if (is_duplicate) {
        lv_label_set_text(s_lbl_status_tag, i18n_t(STR_SCAN_DUPLICATE));
        lv_obj_set_style_bg_color(s_lbl_status_tag, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
    } else {
        lv_label_set_text(s_lbl_status_tag, i18n_t(STR_SCAN_NEW_TAG));
        lv_obj_set_style_bg_color(s_lbl_status_tag, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    }
    lv_obj_set_style_bg_opa(s_lbl_status_tag, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status_tag, lv_color_white(), LV_PART_MAIN);

    lv_label_set_text(s_lbl_hint, is_duplicate ? i18n_t(STR_SCAN_DUPLICATE) : i18n_t(STR_SCAN_NEW_TAG));

    update_controls_enabled(true);

    if (!is_duplicate && s_has_session) {
        s_session.tag_count++;
        update_session_bar();
    }

    screen_scan_flash(is_duplicate);

    if (is_duplicate && s_has_session) {
        tag_record_t rec;
        if (session_get_tag(eid, &rec)) {
            strncpy(s_animal_note, rec.notes, sizeof(s_animal_note) - 1);
            s_animal_note[sizeof(s_animal_note) - 1] = '\0';
            s_tag_has_audio = rec.has_audio;
            s_tag_audio_seq = rec.audio_seq;

            switch (s_session.type) {
                case SESSION_TYPE_WEIGHING:
                    s_weight_kg = rec.weight_kg;
                    update_weight_label();
                    break;
                case SESSION_TYPE_PREGNANCY:
                    s_preg = rec.pregnancy;
                    update_preg_buttons();
                    break;
                case SESSION_TYPE_TEST:
                    s_test_result = rec.test_result;
                    update_test_buttons();
                    break;
                default: break;
            }
        }
    } else {
        s_animal_note[0] = '\0';
        s_tag_has_audio = false;
        s_tag_audio_seq = s_has_session ? (uint16_t)session_get_tag_count() : 0;
        if (s_has_session) {
            switch (s_session.type) {
                case SESSION_TYPE_PREGNANCY:
                    s_preg = PREGNANCY_UNKNOWN;
                    update_preg_buttons();
                    break;
                case SESSION_TYPE_TEST:
                    s_test_result = TEST_INCONCLUSIVE;
                    update_test_buttons();
                    break;
                default: break;
            }
            // Weighing: s_weight_kg intentionally kept from last scan
        }
    }
}

bool screen_scan_get_record(tag_record_t *out) {
    if (!out || !s_eid_pending || !s_has_session) return false;

    memset(out, 0, sizeof(*out));
    strncpy(out->eid, s_current_eid, sizeof(out->eid) - 1);
    out->scanned_at = time(NULL);
    strncpy(out->notes, s_animal_note, sizeof(out->notes) - 1);
    out->has_audio = s_tag_has_audio ? 1 : 0;
    out->audio_seq = s_tag_audio_seq;

    switch (s_session.type) {
        case SESSION_TYPE_WEIGHING:
            out->weight_kg = s_weight_kg;
            break;
        case SESSION_TYPE_PREGNANCY:
            out->pregnancy = s_preg;
            break;
        case SESSION_TYPE_TEST:
            out->test_result = s_test_result;
            break;
        case SESSION_TYPE_VACCINATION: {
            char buf[256] = {0};
            for (int i = 0; i < s_session.vax_count; i++) {
                char name[VACCINE_NAME_MAX];
                if (vaccine_get_name(s_session.vax_ids[i], name, sizeof(name))) {
                    if (i > 0) strncat(buf, ", ", sizeof(buf) - strlen(buf) - 1);
                    strncat(buf, name, sizeof(buf) - strlen(buf) - 1);
                }
            }
            strncpy(out->vaccines, buf, sizeof(out->vaccines) - 1);
            break;
        }
        case SESSION_TYPE_REMOVAL:
            snprintf(out->removal_reason, sizeof(out->removal_reason), "%s", s_session.name);
            break;
        default: break;
    }
    return true;
}

void screen_scan_clear_pending(void) {
    s_current_eid[0] = '\0';
    s_eid_pending = false;
    lv_label_set_text(s_lbl_eid, "---");
    set_status_ready();
    lv_label_set_text(s_lbl_hint, i18n_t(STR_SCAN_READY));
    s_animal_note[0] = '\0';
    s_tag_has_audio = false;
    s_tag_audio_seq = 0;
    s_preg = PREGNANCY_UNKNOWN;
    s_test_result = TEST_INCONCLUSIVE;
    update_preg_buttons();
    update_test_buttons();
    // Do NOT reset weight — persists within session
    update_controls_enabled(false);
}

void screen_scan_update_count(uint32_t count) {
    if (s_has_session) {
        s_session.tag_count = count;
    }
    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%s %" PRIu32, i18n_t(STR_SCAN_COUNT_LABEL), count);
    lv_label_set_text(s_lbl_scan_count, count_str);
}

void screen_scan_show_status(const char *msg) {
    lv_label_set_text(s_lbl_status_tag, msg);
    if (s_status_timer) lv_timer_del(s_status_timer);
    s_status_timer = lv_timer_create(clear_status_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_status_timer, 1);
}

void screen_scan_flash(bool duplicate) {
    lv_color_t color = duplicate ? lv_palette_main(LV_PALETTE_RED) : lv_palette_main(LV_PALETTE_GREEN);
    lv_obj_set_style_bg_color(s_flash_overlay, color, LV_PART_MAIN);
    lv_obj_clear_flag(s_flash_overlay, LV_OBJ_FLAG_HIDDEN);
    if (s_flash_timer) lv_timer_del(s_flash_timer);
    s_flash_timer = lv_timer_create(hide_flash_cb, 600, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

void screen_scan_refresh_language(void) {
    lv_label_set_text(s_lbl_no_session, i18n_t(STR_SCAN_NO_SESSION));
    lv_label_set_text(s_lbl_btn_go_sessions, i18n_t(STR_SESSION_TITLE));
    lv_label_set_text(s_lbl_vax_title, i18n_t(STR_SESSION_SELECT_VAX));

    lv_label_set_text(s_lbl_preg[0], i18n_t(STR_PREG_UNKNOWN));
    lv_label_set_text(s_lbl_preg[1], i18n_t(STR_PREG_NO));
    lv_label_set_text(s_lbl_preg[2], i18n_t(STR_PREG_REJECTED));
    lv_label_set_text(s_lbl_preg[3], i18n_t(STR_PREG_SMALL));
    lv_label_set_text(s_lbl_preg[4], i18n_t(STR_PREG_MEDIUM));
    lv_label_set_text(s_lbl_preg[5], i18n_t(STR_PREG_BIG));

    lv_label_set_text(s_lbl_test[0], i18n_t(STR_TEST_INCONCLUSIVE));
    lv_label_set_text(s_lbl_test[1], i18n_t(STR_TEST_POSITIVE));
    lv_label_set_text(s_lbl_test[2], i18n_t(STR_TEST_NEGATIVE));

    update_session_bar();

    if (!s_eid_pending) {
        set_status_ready();
    }
}
