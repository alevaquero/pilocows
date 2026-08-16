#include "screen_session_new.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "ui_popup.h"
#include "ui_text_entry.h"
#include "screen_audio_note.h"
#include "session_storage.h"
#include "bsp_mic.h"
#include "ui_icons.h"
#include "i18n.h"
#include "strings_en.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "scr_sess_new";

// ── Header (fixed, always visible) ───────────────────────────────────────────
static lv_obj_t *s_hdr;

// ── Static label refs ────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_type_label;
static lv_obj_t *s_lbl_name_label;
static lv_obj_t *s_lbl_note_label;

// ── Discard confirmation overlay ─────────────────────────────────────────────
static lv_obj_t *s_confirm_panel;
static lv_obj_t *s_lbl_confirm_msg;
static lv_obj_t *s_lbl_confirm_ok;
static lv_obj_t *s_lbl_confirm_cancel;
static lv_obj_t *s_lbl_confirm_save;

// ── Scrollable body container (below header) ─────────────────────────────────
static lv_obj_t *s_content;

// ── Widgets (children of s_content) ──────────────────────────────────────────
static lv_obj_t *s_dd_type;
static lv_obj_t *s_lbl_type_value; // edit mode only: plain read-only text in place of s_dd_type
static lv_obj_t *s_ta_name;
static lv_obj_t *s_ta_note;
static lv_obj_t *s_btn_note_audio;
static lv_obj_t *s_lbl_audio_status;
static lv_obj_t *s_lbl_voice_note;

// ── Staged session-note audio ────────────────────────────────────────────────
// The session doesn't exist yet at record time (mirrors how s_ta_note stages
// text until on_create() runs), so a fresh recording is copied into this PSRAM
// buffer and only written to SD (session_save_note_audio) once the session is
// actually created.
#define STAGED_AUDIO_MAX_SAMPLES (10 * MIC_SAMPLE_RATE_HZ)
static int16_t *s_staged_audio = NULL;
static size_t s_staged_audio_samples = 0;
static bool s_has_staged_audio = false;

// screen_audio_note is a full registered screen (not an overlay), so
// navigating to it and back re-fires LV_EVENT_SCREEN_LOADED on this screen.
// Without this flag, on_screen_loaded() can't tell "returning from the
// recorder" apart from "opening this form fresh", and would wipe out
// whatever was just staged (and the rest of the in-progress form) on return.
static bool s_returning_from_recorder = false;

// ── Edit mode (screen_session_new_edit) ──────────────────────────────────────
// Reuses this whole screen to edit an existing session's name/note/voice note
// (type and its vaccine/test sub-fields are shown but locked — only
// session_create() ever sets those). Saving calls session_update_name()/
// session_save_note()/session_save_note_audio() instead of session_create().
static bool s_edit_mode = false;
static uint32_t s_edit_session_id = 0;
static bool s_entering_edit = false; // on_screen_loaded: populate from storage instead of resetting blank
static bool s_edit_orig_has_audio = false; // m.has_note_audio at load time, before any edits this visit
static bool s_note_audio_deleted = false;  // edit mode: user deleted the original (pre-existing) SD audio

// ── Vaccine checkboxes ────────────────────────────────────────────────────────
static lv_obj_t *s_vax_section;
static lv_obj_t *s_lbl_vax;
static lv_obj_t *s_vax_cbs[VACCINE_LIST_MAX];
static uint8_t s_vax_ids[VACCINE_LIST_MAX];
static int s_vax_count = 0;

// ── Test dropdown ─────────────────────────────────────────────────────────────
static lv_obj_t *s_test_section;
static lv_obj_t *s_lbl_test;
static lv_obj_t *s_dd_test;
static test_cfg_t s_test_list_buf[TEST_LIST_MAX];
static int s_test_list_count = 0;
static uint8_t s_selected_test_id = 0;

static lv_obj_t *s_scr = NULL;

// Row positions relative to s_content. Label-above/field-below (was label-left/
// field-right) since a 480px-wide canvas can't fit both side by side.
#define ROW_TYPE_LBL_Y  12
#define ROW_TYPE_DD_Y   47
#define ROW_NAME_LBL_Y  130
#define ROW_NAME_TA_Y   165
#define ROW_AUDIO_Y     248 // dedicated "Voice Note" row, ahead of the Note field
#define ROW_AUDIO_H     72
#define ROW_NOTE_LBL_Y  337
#define ROW_NOTE_TA_Y   372
#define ROW_VAX_Y       529
#define ROW_TEST_Y      529

static void build_type_options(char *buf, size_t len) {
    const char *types[] = {
        i18n_t(STR_EVENT_GENERAL),
        i18n_t(STR_EVENT_WEIGHING),
        i18n_t(STR_EVENT_VACCINATION),
        i18n_t(STR_EVENT_PREGNANCY),
        i18n_t(STR_EVENT_TEST),
        i18n_t(STR_EVENT_REMOVAL),
    };
    buf[0] = '\0';
    for (int i = 0; i < 6; i++) {
        if (i > 0) strncat(buf, "\n", len - strlen(buf) - 1);
        strncat(buf, types[i], len - strlen(buf) - 1);
    }
}

static session_type_t selected_type(void) {
    return (session_type_t)lv_dropdown_get_selected(s_dd_type);
}

// Type name for the edit-mode read-only label — same options as
// build_type_options(), just indexed directly instead of joined into one
// dropdown-options string.
static const char *type_label_text(session_type_t type) {
    switch (type) {
        case SESSION_TYPE_WEIGHING:    return i18n_t(STR_EVENT_WEIGHING);
        case SESSION_TYPE_VACCINATION: return i18n_t(STR_EVENT_VACCINATION);
        case SESSION_TYPE_PREGNANCY:   return i18n_t(STR_EVENT_PREGNANCY);
        case SESSION_TYPE_TEST:        return i18n_t(STR_EVENT_TEST);
        case SESSION_TYPE_REMOVAL:     return i18n_t(STR_EVENT_REMOVAL);
        default:                       return i18n_t(STR_EVENT_GENERAL);
    }
}

static void update_vax_visibility(void) {
    if (selected_type() == SESSION_TYPE_VACCINATION) {
        lv_obj_clear_flag(s_vax_section, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_vax_section, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_test_visibility(void) {
    if (selected_type() == SESSION_TYPE_TEST) {
        lv_obj_clear_flag(s_test_section, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_test_section, LV_OBJ_FLAG_HIDDEN);
    }
}

static void populate_tests(void) {
    s_test_list_count = (int)test_list(s_test_list_buf, TEST_LIST_MAX);

    char opts[TEST_LIST_MAX * (TEST_NAME_MAX + 1) + 32];
    opts[0] = '\0';
    for (int i = 0; i < s_test_list_count; i++) {
        if (i > 0) strncat(opts, "\n", sizeof(opts) - strlen(opts) - 1);
        strncat(opts, s_test_list_buf[i].name, sizeof(opts) - strlen(opts) - 1);
    }
    if (s_test_list_count == 0) {
        strncat(opts, "-", sizeof(opts) - strlen(opts) - 1);
    }
    lv_dropdown_set_options(s_dd_test, opts);
    lv_dropdown_set_selected(s_dd_test, 0);
    s_selected_test_id = (s_test_list_count > 0) ? s_test_list_buf[0].id : 0;
}

static void populate_vaccines(void) {
    for (int i = 0; i < s_vax_count; i++) {
        lv_obj_del(s_vax_cbs[i]);
        s_vax_cbs[i] = NULL;
    }

    vaccine_cfg_t vlist[VACCINE_LIST_MAX];
    s_vax_count = (int)vaccine_list(vlist, VACCINE_LIST_MAX);
    for (int i = 0; i < s_vax_count; i++) {
        s_vax_ids[i] = vlist[i].id;
        s_vax_cbs[i] = lv_checkbox_create(s_vax_section);
        lv_checkbox_set_text(s_vax_cbs[i], vlist[i].name);
        lv_obj_set_style_text_font(s_vax_cbs[i], &lv_font_app_28, LV_PART_MAIN);
        lv_obj_set_pos(s_vax_cbs[i], 0, 39 + i * 45);
    }
    int outer_h = (s_vax_count > 0) ? (57 + s_vax_count * 45) : 60;
    lv_obj_set_size(s_vax_section, 440, outer_h);
}

static void on_back(lv_event_t *e) {
    (void)e;
    lv_obj_clear_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_discard_confirm(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    bool was_edit = s_edit_mode;
    s_edit_mode = false;
    ui_manager_show(was_edit ? SCREEN_SESSION_LIST : SCREEN_SESSION_MENU);
}

static void on_discard_cancel(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_type_changed(lv_event_t *e) {
    (void)e;
    char def_name[SESSION_NAME_MAX];
    session_build_default_name(selected_type(), def_name, sizeof(def_name));
    lv_textarea_set_text(s_ta_name, def_name);
    update_vax_visibility();
    update_test_visibility();
}

static void on_test_dd_changed(lv_event_t *e) {
    (void)e;
    uint16_t sel = lv_dropdown_get_selected(s_dd_test);
    s_selected_test_id = (sel < (uint16_t)s_test_list_count) ? s_test_list_buf[sel].id : 0;
}

// Name/note are edited via the shared ui_text_entry modal (tap to open); these
// textareas just display the current value.
static void on_name_confirm(const char *text, void *user_data) {
    (void)user_data;
    lv_textarea_set_text(s_ta_name, text);
}

static void on_name_clicked(lv_event_t *e) {
    (void)e;
    ui_text_entry_cfg_t cfg = {
        .label = i18n_t(STR_SESSION_NAME),
        .initial_text = lv_textarea_get_text(s_ta_name),
        .placeholder = NULL,
        .multiline = false,
        .password = false,
        .max_length = SESSION_NAME_MAX - 1,
        .on_confirm = on_name_confirm,
        .on_cancel = NULL,
        .user_data = NULL,
    };
    ui_text_entry_show(&cfg);
}

static void on_note_confirm(const char *text, void *user_data) {
    (void)user_data;
    lv_textarea_set_text(s_ta_note, text);
}

static void on_note_clicked(lv_event_t *e) {
    (void)e;
    ui_text_entry_cfg_t cfg = {
        .label = i18n_t(STR_NOTE),
        .initial_text = lv_textarea_get_text(s_ta_note),
        .placeholder = i18n_t(STR_SESSION_NOTE_PLACEHOLDER),
        .multiline = true,
        .password = false,
        .max_length = SESSION_NOTE_MAX - 1,
        .on_confirm = on_note_confirm,
        .on_cancel = NULL,
        .user_data = NULL,
    };
    ui_text_entry_show(&cfg);
}

// ── Session-note audio recorder ──────────────────────────────────────────────

// True if there's currently a voice note to play, whether that's a fresh
// recording made this visit (create or edit mode) or — edit mode only — the
// original SD recording, untouched so far this visit.
static bool has_any_note_audio(void) {
    if (s_has_staged_audio) return true;
    if (s_edit_mode && s_edit_orig_has_audio && !s_note_audio_deleted) return true;
    return false;
}

static void update_note_audio_btn_style(void) {
    bool has_audio = has_any_note_audio();
    lv_color_t c = has_audio ? lv_palette_main(LV_PALETTE_GREEN) : lv_palette_main(LV_PALETTE_GREY);
    lv_obj_set_style_bg_color(s_btn_note_audio, c, LV_PART_MAIN);
    lv_label_set_text(s_lbl_audio_status,
                       i18n_t(has_audio ? STR_AUDIO_NOTE_HAS_RECORDING : STR_AUDIO_NOTE_NONE));
}

static void on_note_audio_recorded(const int16_t *pcm, size_t n_samples, void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "on_note_audio_recorded: n_samples=%zu staged_buf=%p", n_samples, (void *)s_staged_audio);
    if (n_samples > STAGED_AUDIO_MAX_SAMPLES) n_samples = STAGED_AUDIO_MAX_SAMPLES;
    if (s_staged_audio && n_samples > 0) {
        memcpy(s_staged_audio, pcm, n_samples * sizeof(int16_t));
        s_staged_audio_samples = n_samples;
        s_has_staged_audio = true;
        ESP_LOGI(TAG, "on_note_audio_recorded: staged %zu samples", s_staged_audio_samples);
    } else {
        ESP_LOGW(TAG, "on_note_audio_recorded: NOT staged (staged_buf=%p n_samples=%zu)", (void *)s_staged_audio, n_samples);
    }
    update_note_audio_btn_style();
}

static void on_note_audio_deleted(void *user_data) {
    (void)user_data;
    ESP_LOGI(TAG, "on_note_audio_deleted: clearing staged audio");
    s_has_staged_audio = false;
    s_staged_audio_samples = 0;
    // Edit mode: also remember to delete the original SD file on Save, if
    // this session had one and the user didn't replace it with a new take.
    if (s_edit_mode) s_note_audio_deleted = true;
    update_note_audio_btn_style();
}

static void on_note_audio_clicked(lv_event_t *e) {
    (void)e;
    ESP_LOGI(TAG, "on_note_audio_clicked: opening recorder (has_staged=%d edit=%d)", s_has_staged_audio, s_edit_mode);
    s_returning_from_recorder = true;

    // Edit mode, original (unmodified-this-visit) SD recording: play from
    // the real file. Every other case (create mode, or a fresh recording
    // made this visit in either mode) plays from the in-RAM staged buffer —
    // create mode has no SD file yet, and a fresh edit-mode recording
    // hasn't been saved over the original yet either.
    bool use_sd_path = s_edit_mode && !s_has_staged_audio && s_edit_orig_has_audio && !s_note_audio_deleted;
    char wav_path[64] = "";
    if (use_sd_path) {
        session_note_audio_path(s_edit_session_id, wav_path, sizeof(wav_path));
    }

    audio_note_cfg_t cfg = {
        .title = i18n_t(STR_SESSION_NOTE),
        .has_existing = has_any_note_audio(),
        .existing_wav_path = use_sd_path ? wav_path : NULL,
        .existing_pcm = (!use_sd_path && s_has_staged_audio) ? s_staged_audio : NULL,
        .existing_pcm_samples = (!use_sd_path && s_has_staged_audio) ? s_staged_audio_samples : 0,
        .return_screen = SCREEN_SESSION_NEW,
        .on_recorded = on_note_audio_recorded,
        .on_deleted = on_note_audio_deleted,
        .user_data = NULL,
    };
    screen_audio_note_show(&cfg);
}

static void on_save(lv_event_t *e) {
    (void)e;

    const char *name = lv_textarea_get_text(s_ta_name);

    if (s_edit_mode) {
        uint32_t id = s_edit_session_id;
        if (name[0]) session_update_name(id, name);
        const char *note = lv_textarea_get_text(s_ta_note);
        session_save_note(id, note ? note : "");

        if (s_has_staged_audio) {
            esp_err_t aerr = session_save_note_audio(id, s_staged_audio, s_staged_audio_samples);
            ESP_LOGI(TAG, "on_save (edit): session_save_note_audio -> %s", esp_err_to_name(aerr));
        } else if (s_note_audio_deleted) {
            esp_err_t aerr = session_delete_note_audio(id);
            ESP_LOGI(TAG, "on_save (edit): session_delete_note_audio -> %s", esp_err_to_name(aerr));
        }

        ESP_LOGI(TAG, "Saved edits to session %" PRIu32, id);
        s_edit_mode = false;
        ui_manager_show(SCREEN_SESSION_LIST);
        return;
    }

    session_type_t type = selected_type();

    uint8_t vax_ids[SESSION_VAX_MAX];
    uint8_t vax_count = 0;
    if (type == SESSION_TYPE_VACCINATION) {
        for (int i = 0; i < s_vax_count && vax_count < SESSION_VAX_MAX; i++) {
            if (lv_obj_get_state(s_vax_cbs[i]) & LV_STATE_CHECKED) {
                vax_ids[vax_count++] = s_vax_ids[i];
            }
        }
        if (vax_count == 0) {
            ui_popup_error(i18n_t("Please select at least one vaccine."));
            return;
        }
    }

    uint32_t new_id = session_create(name[0] ? name : NULL, type,
                                      vax_count ? vax_ids : NULL, vax_count, s_selected_test_id);
    if (new_id != 0) {
        ESP_LOGI(TAG, "Created session %" PRIu32, new_id);
        session_set_active(new_id);
        const char *note = lv_textarea_get_text(s_ta_note);
        if (note && note[0]) session_save_note(new_id, note);
        ESP_LOGI(TAG, "on_save (create): has_staged_audio=%d samples=%zu", s_has_staged_audio, s_staged_audio_samples);
        if (s_has_staged_audio) {
            esp_err_t aerr = session_save_note_audio(new_id, s_staged_audio, s_staged_audio_samples);
            ESP_LOGI(TAG, "on_save (create): session_save_note_audio -> %s", esp_err_to_name(aerr));
        }
        ui_manager_show(SCREEN_SCAN);
    } else {
        ESP_LOGE(TAG, "session_create failed");
    }
}

static void on_discard_save(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    on_save(NULL);
}

// Pre-populates the form from an existing session's saved data and locks
// the fields that stay fixed for the session's lifetime (type, and its
// vaccine/test sub-fields — shown for context, not editable here).
static void load_session_for_edit(void) {
    session_meta_t m;
    if (session_get(s_edit_session_id, &m) != ESP_OK) {
        ESP_LOGE(TAG, "load_session_for_edit: session %" PRIu32 " not found", s_edit_session_id);
        s_edit_mode = false;
        ui_manager_show(SCREEN_SESSION_LIST);
        return;
    }

    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_EDIT));
    lv_label_set_text(s_lbl_confirm_msg, i18n_t(STR_SESSION_CONFIRM_DISCARD_EDIT));

    lv_textarea_set_text(s_ta_name, m.name);
    lv_textarea_set_text(s_ta_note, m.note);

    lv_dropdown_set_selected(s_dd_type, (uint16_t)m.type);
    lv_obj_add_state(s_dd_type, LV_STATE_DISABLED);
    lv_obj_add_flag(s_dd_type, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_lbl_type_value, type_label_text(m.type));
    lv_obj_clear_flag(s_lbl_type_value, LV_OBJ_FLAG_HIDDEN);

    populate_vaccines();
    for (int i = 0; i < s_vax_count; i++) {
        bool checked = false;
        for (int j = 0; j < m.vax_count; j++) {
            if (s_vax_ids[i] == m.vax_ids[j]) { checked = true; break; }
        }
        if (checked) lv_obj_add_state(s_vax_cbs[i], LV_STATE_CHECKED);
        lv_obj_add_state(s_vax_cbs[i], LV_STATE_DISABLED);
    }
    update_vax_visibility();

    populate_tests();
    for (int i = 0; i < s_test_list_count; i++) {
        if (s_test_list_buf[i].id == m.test_id) {
            lv_dropdown_set_selected(s_dd_test, (uint16_t)i);
            s_selected_test_id = m.test_id;
            break;
        }
    }
    lv_obj_add_state(s_dd_test, LV_STATE_DISABLED);
    update_test_visibility();

    s_has_staged_audio = false;
    s_staged_audio_samples = 0;
    s_edit_orig_has_audio = m.has_note_audio;
    s_note_audio_deleted = false;
    update_note_audio_btn_style();
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;

    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);

    if (s_returning_from_recorder) {
        s_returning_from_recorder = false;
        ESP_LOGI(TAG, "on_screen_loaded: returning from recorder, preserving form state");
        return; // preserve name/type/note/vaccines/staged-audio exactly as they were
    }

    if (s_entering_edit) {
        s_entering_edit = false;
        ESP_LOGI(TAG, "on_screen_loaded: entering edit mode for session %" PRIu32, s_edit_session_id);
        load_session_for_edit();
        return;
    }

    // Fresh "New Session" — also clears any lingering state/labels from a
    // previous edit-mode visit (type/test dropdowns get re-disabled and
    // re-populated by populate_vaccines()/edit mode each time, but aren't
    // automatically re-enabled otherwise since they're persistent widgets).
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_NEW));
    lv_label_set_text(s_lbl_confirm_msg, i18n_t(STR_SESSION_CONFIRM_DISCARD));
    lv_obj_clear_state(s_dd_type, LV_STATE_DISABLED);
    lv_obj_clear_flag(s_dd_type, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_type_value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(s_dd_test, LV_STATE_DISABLED);

    lv_textarea_set_text(s_ta_note, "");
    lv_dropdown_set_selected(s_dd_type, 0);

    s_has_staged_audio = false;
    s_staged_audio_samples = 0;
    s_edit_orig_has_audio = false;
    s_note_audio_deleted = false;
    update_note_audio_btn_style();

    char def_name[SESSION_NAME_MAX];
    session_build_default_name(selected_type(), def_name, sizeof(def_name));
    lv_textarea_set_text(s_ta_name, def_name);

    populate_vaccines();
    update_vax_visibility();

    populate_tests();
    update_test_visibility();
}

void screen_session_new_edit(uint32_t session_id) {
    s_edit_mode = true;
    s_edit_session_id = session_id;
    s_entering_edit = true;
    ui_manager_show(SCREEN_SESSION_NEW);
}

void screen_session_new_create(void) {
    s_staged_audio = heap_caps_malloc(STAGED_AUDIO_MAX_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_staged_audio) {
        ESP_LOGE(TAG, "Failed to allocate %d-byte staged audio buffer", (int)(STAGED_AUDIO_MAX_SAMPLES * sizeof(int16_t)));
    }

    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header (fixed — not inside s_content) ───────────────────────────────
    s_hdr = lv_obj_create(s_scr);
    lv_obj_set_size(s_hdr, 480, 80);
    lv_obj_set_pos(s_hdr, 0, 0);
    lv_obj_clear_flag(s_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_hdr, lv_color_hex(0x2c3e50), 0);

    lv_obj_t *btn_back = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_back, 70, 70);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 6);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    ui_icon_create(btn_back, UI_SYMBOL_BACK, lv_color_white(), &lv_font_app_30);

    s_lbl_title = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_NEW));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_create = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_create, 70, 70);
    lv_obj_align(btn_create, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_border_width(btn_create, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_create, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_create, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_create, 6);
    lv_obj_add_event_cb(btn_create, on_save, LV_EVENT_CLICKED, NULL);
    ui_icon_create(btn_create, UI_SYMBOL_SAVE, lv_color_white(), &lv_font_app_30);

    // ── Scrollable content area (below header) ──────────────────────────────
    s_content = lv_obj_create(s_scr);
    lv_obj_set_size(s_content, 480, 720);
    lv_obj_set_pos(s_content, 0, 80);
    lv_obj_set_style_border_width(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_content, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);

    // ── Type row ──────────────────────────────────────────────────────────
    s_lbl_type_label = lv_label_create(s_content);
    lv_label_set_text(s_lbl_type_label, i18n_t(STR_SESSION_TYPE));
    lv_obj_set_style_text_font(s_lbl_type_label, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_type_label, 20, ROW_TYPE_LBL_Y);

    s_dd_type = lv_dropdown_create(s_content);
    lv_obj_set_size(s_dd_type, 440, 63);
    lv_obj_set_pos(s_dd_type, 20, ROW_TYPE_DD_Y);
    lv_obj_set_style_text_font(s_dd_type, &lv_font_app_30, LV_PART_MAIN);
    char type_opts[256];
    build_type_options(type_opts, sizeof(type_opts));
    lv_dropdown_set_options(s_dd_type, type_opts);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_dd_type), &lv_font_app_30, LV_PART_MAIN);
    lv_obj_add_event_cb(s_dd_type, on_type_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // Edit mode shows this instead of s_dd_type — a disabled dropdown still
    // looks tappable, which read as broken/confusing since type can't
    // actually be changed after a session is created.
    s_lbl_type_value = lv_label_create(s_content);
    lv_obj_set_style_text_font(s_lbl_type_value, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_type_value, 20, ROW_TYPE_DD_Y + 16);
    lv_obj_add_flag(s_lbl_type_value, LV_OBJ_FLAG_HIDDEN);

    // ── Name row ──────────────────────────────────────────────────────────
    s_lbl_name_label = lv_label_create(s_content);
    lv_label_set_text(s_lbl_name_label, i18n_t(STR_SESSION_NAME));
    lv_obj_set_style_text_font(s_lbl_name_label, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_name_label, 20, ROW_NAME_LBL_Y);

    s_ta_name = lv_textarea_create(s_content);
    lv_obj_set_size(s_ta_name, 440, 66);
    lv_obj_set_pos(s_ta_name, 20, ROW_NAME_TA_Y);
    lv_obj_set_style_text_font(s_ta_name, &lv_font_app_30, LV_PART_MAIN);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_max_length(s_ta_name, SESSION_NAME_MAX - 1);
    lv_obj_set_scrollbar_mode(s_ta_name, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_name, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ta_name, on_name_clicked, LV_EVENT_CLICKED, NULL);

    // ── Voice Note row (own row, ahead of the text Note field) ──────────────
    lv_obj_t *audio_row = lv_obj_create(s_content);
    lv_obj_set_size(audio_row, 440, ROW_AUDIO_H);
    lv_obj_set_pos(audio_row, 20, ROW_AUDIO_Y);
    lv_obj_clear_flag(audio_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(audio_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(audio_row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(audio_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(audio_row, 0, LV_PART_MAIN);

    s_lbl_voice_note = lv_label_create(audio_row);
    lv_label_set_text(s_lbl_voice_note, i18n_t(STR_SESSION_VOICE_NOTE));
    lv_obj_set_style_text_font(s_lbl_voice_note, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_voice_note, 0, 4);

    s_lbl_audio_status = lv_label_create(audio_row);
    lv_obj_set_style_text_font(s_lbl_audio_status, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_audio_status, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_audio_status, 0, 40);

    s_btn_note_audio = lv_btn_create(audio_row);
    lv_obj_set_size(s_btn_note_audio, 56, 56);
    lv_obj_align(s_btn_note_audio, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(s_btn_note_audio, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_note_audio, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    lv_obj_add_event_cb(s_btn_note_audio, on_note_audio_clicked, LV_EVENT_CLICKED, NULL);
    ui_icon_create(s_btn_note_audio, UI_SYMBOL_MIC, lv_color_white(), &lv_font_app_28);

    // ── Note row ──────────────────────────────────────────────────────────
    s_lbl_note_label = lv_label_create(s_content);
    lv_label_set_text(s_lbl_note_label, i18n_t(STR_NOTE));
    lv_obj_set_style_text_font(s_lbl_note_label, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_note_label, 20, ROW_NOTE_LBL_Y);

    s_ta_note = lv_textarea_create(s_content);
    lv_obj_set_size(s_ta_note, 440, 140);
    lv_obj_set_pos(s_ta_note, 20, ROW_NOTE_TA_Y);
    lv_obj_set_style_text_font(s_ta_note, &lv_font_app_30, LV_PART_MAIN);
    lv_textarea_set_one_line(s_ta_note, false);
    lv_textarea_set_max_length(s_ta_note, SESSION_NOTE_MAX - 1);
    lv_textarea_set_placeholder_text(s_ta_note, i18n_t(STR_SESSION_NOTE_PLACEHOLDER));
    lv_obj_set_scrollbar_mode(s_ta_note, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_note, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ta_note, on_note_clicked, LV_EVENT_CLICKED, NULL);

    // ── Vaccine section ──────────────────────────────────────────────────────
    s_vax_section = lv_obj_create(s_content);
    lv_obj_set_size(s_vax_section, 440, 60);
    lv_obj_set_pos(s_vax_section, 20, ROW_VAX_Y);
    lv_obj_set_style_border_width(s_vax_section, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vax_section, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_vax_section, 9, LV_PART_MAIN);

    s_lbl_vax = lv_label_create(s_vax_section);
    lv_label_set_text(s_lbl_vax, i18n_t(STR_SESSION_SELECT_VAX));
    lv_obj_set_style_text_font(s_lbl_vax, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax, 0, 0);

    lv_obj_add_flag(s_vax_section, LV_OBJ_FLAG_HIDDEN);

    // ── Test section ──────────────────────────────────────────────────────
    s_test_section = lv_obj_create(s_content);
    lv_obj_set_size(s_test_section, 440, 117);
    lv_obj_set_pos(s_test_section, 20, ROW_TEST_Y);
    lv_obj_set_style_border_width(s_test_section, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_test_section, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_test_section, 9, LV_PART_MAIN);
    lv_obj_clear_flag(s_test_section, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_test = lv_label_create(s_test_section);
    lv_label_set_text(s_lbl_test, i18n_t(STR_SESSION_SELECT_TEST));
    lv_obj_set_style_text_font(s_lbl_test, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_test, 0, 0);

    s_dd_test = lv_dropdown_create(s_test_section);
    lv_obj_set_size(s_dd_test, 420, 66);
    lv_obj_align(s_dd_test, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_obj_set_style_text_font(s_dd_test, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_dd_test), &lv_font_app_30, LV_PART_MAIN);
    lv_dropdown_set_options(s_dd_test, "-");
    lv_obj_add_event_cb(s_dd_test, on_test_dd_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_add_flag(s_test_section, LV_OBJ_FLAG_HIDDEN);

    memset(s_vax_cbs, 0, sizeof(s_vax_cbs));

    // ── Discard confirmation overlay ─────────────────────────────────────────
    s_confirm_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_confirm_panel, 480, 800);
    lv_obj_set_pos(s_confirm_panel, 0, 0);
    lv_obj_clear_flag(s_confirm_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_confirm_panel, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_confirm_panel, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_confirm_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_confirm_panel, 0, LV_PART_MAIN);
    lv_obj_add_flag(s_confirm_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card = lv_obj_create(s_confirm_panel);
    lv_obj_set_size(card, 420, 332);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 24, LV_PART_MAIN);

    s_lbl_confirm_msg = lv_label_create(card);
    lv_label_set_text(s_lbl_confirm_msg, i18n_t(STR_SESSION_CONFIRM_DISCARD));
    lv_obj_set_style_text_font(s_lbl_confirm_msg, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_width(s_lbl_confirm_msg, 372);
    lv_label_set_long_mode(s_lbl_confirm_msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(s_lbl_confirm_msg, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *btn_save = lv_btn_create(card);
    lv_obj_set_size(btn_save, 372, 60);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -78);
    lv_obj_set_style_bg_color(btn_save, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_save, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_save, 10);
    lv_obj_add_event_cb(btn_save, on_discard_save, LV_EVENT_CLICKED, NULL);
    s_lbl_confirm_save = lv_label_create(btn_save);
    lv_label_set_text(s_lbl_confirm_save, i18n_t(STR_SETTINGS_SAVE));
    lv_obj_set_style_text_font(s_lbl_confirm_save, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_confirm_save, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(s_lbl_confirm_save);

    lv_obj_t *btn_cancel = lv_btn_create(card);
    lv_obj_set_size(btn_cancel, 180, 66);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_border_width(btn_cancel, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_cancel, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_cancel, 10);
    lv_obj_add_event_cb(btn_cancel, on_discard_cancel, LV_EVENT_CLICKED, NULL);
    s_lbl_confirm_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(s_lbl_confirm_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_font(s_lbl_confirm_cancel, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_confirm_cancel, lv_color_black(), LV_PART_MAIN);
    lv_obj_center(s_lbl_confirm_cancel);

    lv_obj_t *btn_ok = lv_btn_create(card);
    lv_obj_set_size(btn_ok, 180, 66);
    lv_obj_align(btn_ok, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_ok, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_ok, 10);
    lv_obj_add_event_cb(btn_ok, on_discard_confirm, LV_EVENT_CLICKED, NULL);
    s_lbl_confirm_ok = lv_label_create(btn_ok);
    lv_label_set_text(s_lbl_confirm_ok, i18n_t(STR_SESSION_DISCARD));
    lv_obj_set_style_text_font(s_lbl_confirm_ok, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_confirm_ok, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(s_lbl_confirm_ok);

    ESP_LOGI(TAG, "Session new screen created");
}

void screen_session_new_load(void) {
    lv_scr_load(s_scr);
}

void screen_session_new_refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(s_edit_mode ? STR_SESSION_EDIT : STR_SESSION_NEW));
    lv_label_set_text(s_lbl_type_label, i18n_t(STR_SESSION_TYPE));
    lv_label_set_text(s_lbl_name_label, i18n_t(STR_SESSION_NAME));
    lv_label_set_text(s_lbl_note_label, i18n_t(STR_NOTE));
    lv_label_set_text(s_lbl_voice_note, i18n_t(STR_SESSION_VOICE_NOTE));
    update_note_audio_btn_style();
    lv_label_set_text(s_lbl_vax, i18n_t(STR_SESSION_SELECT_VAX));
    lv_label_set_text(s_lbl_test, i18n_t(STR_SESSION_SELECT_TEST));
    lv_label_set_text(s_lbl_confirm_msg, i18n_t(s_edit_mode ? STR_SESSION_CONFIRM_DISCARD_EDIT : STR_SESSION_CONFIRM_DISCARD));
    lv_label_set_text(s_lbl_confirm_ok, i18n_t(STR_SESSION_DISCARD));
    lv_label_set_text(s_lbl_confirm_cancel, i18n_t(STR_BTN_CANCEL));
    lv_label_set_text(s_lbl_confirm_save, i18n_t(STR_SETTINGS_SAVE));

    char type_opts[256];
    build_type_options(type_opts, sizeof(type_opts));
    lv_dropdown_set_options(s_dd_type, type_opts);
    if (s_edit_mode) {
        lv_label_set_text(s_lbl_type_value, type_label_text(selected_type()));
    }
}
