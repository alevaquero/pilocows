#include "screen_session_new.h"
#include "ui_manager.h"
#include "ui_popup.h"
#include "lvgl.h"
#include "../i18n/i18n.h"
#include "../i18n/strings_en.h"
#include "../storage/session_storage.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>
#include "fonts.h"

static const char *TAG = "scr_sess_new";

// ── Header (fixed, always visible) ────────────────────────────────────────────
static lv_obj_t *s_hdr;

// ── Static label refs ─────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_back;
static lv_obj_t *s_lbl_type_label;
static lv_obj_t *s_lbl_name_label;
static lv_obj_t *s_lbl_note_label;
static lv_obj_t *s_lbl_btn_create;

// ── Scrollable body container (below header) ──────────────────────────────────
static lv_obj_t *s_content;

// ── Widgets (children of s_content) ───────────────────────────────────────────
static lv_obj_t *s_dd_type;    // dropdown for session type
static lv_obj_t *s_ta_name;    // text area for session name
static lv_obj_t *s_ta_note;    // text area for session note
static lv_obj_t *s_kb;         // keyboard (child of s_scr, overlays content)

// ── Vaccine checkboxes ────────────────────────────────────────────────────────
static lv_obj_t *s_vax_section; // container, hidden unless type=VACCINATION
static lv_obj_t *s_lbl_vax;
static lv_obj_t *s_vax_cbs[VACCINE_LIST_MAX];
static uint8_t   s_vax_ids[VACCINE_LIST_MAX];
static int       s_vax_count = 0;

static lv_obj_t *s_scr = NULL;

// Normal (non-keyboard) positions of body rows, relative to s_content.
// Subtract 40 from old s_scr values (s_hdr was 40 px tall).
#define ROW_TYPE_DD_Y    8
#define ROW_TYPE_LBL_Y   17
#define ROW_NAME_TA_Y    60
#define ROW_NAME_LBL_Y   69
#define ROW_NOTE_TA_Y    118
#define ROW_NOTE_LBL_Y   127
#define ROW_VAX_Y        170

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static void build_type_options(char *buf, size_t len)
{
    // Order must match session_type_t values 0-5
    const char *types[] = {
        i18n_t("General"),
        i18n_t("Weighing"),
        i18n_t("Vaccination"),
        i18n_t("Pregnancy Check"),
        i18n_t("TB Test"),
        i18n_t("Removal"),
    };
    buf[0] = '\0';
    for (int i = 0; i < 6; i++) {
        if (i > 0) strncat(buf, "\n", len - strlen(buf) - 1);
        strncat(buf, types[i], len - strlen(buf) - 1);
    }
}

static session_type_t selected_type(void)
{
    return (session_type_t)lv_dropdown_get_selected(s_dd_type);
}

static void update_vax_visibility(void)
{
    if (selected_type() == SESSION_TYPE_VACCINATION) {
        lv_obj_clear_flag(s_vax_section, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_vax_section, LV_OBJ_FLAG_HIDDEN);
    }
}

static void populate_vaccines(void)
{
    // Remove old checkboxes
    for (int i = 0; i < s_vax_count; i++) {
        lv_obj_del(s_vax_cbs[i]);
        s_vax_cbs[i] = NULL;
    }

    vaccine_cfg_t vlist[VACCINE_LIST_MAX];
    s_vax_count = vaccine_list(vlist, VACCINE_LIST_MAX);
    for (int i = 0; i < s_vax_count; i++) {
        s_vax_ids[i] = vlist[i].id;
        s_vax_cbs[i] = lv_checkbox_create(s_vax_section);
        lv_checkbox_set_text(s_vax_cbs[i], vlist[i].name);
        lv_obj_set_style_text_font(s_vax_cbs[i], &pilocows_font_18, LV_PART_MAIN);
        // Label (s_lbl_vax) takes ~22 px; first checkbox at y=26.
        lv_obj_set_pos(s_vax_cbs[i], 0, 26 + i * 30);
    }
    // Resize container to fit all vaccines so the parent (s_content) can scroll.
    // With pad_all=6: outer h = pad_top(6) + content(26+N*30) + pad_bot(6) = 38+N*30.
    int outer_h = (s_vax_count > 0) ? (38 + s_vax_count * 30) : 40;
    lv_obj_set_size(s_vax_section, 456, outer_h);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks
// ─────────────────────────────────────────────────────────────────────────────

static void on_back(lv_event_t *e)
{
    (void)e;
    ui_manager_show(SCREEN_SESSION_MENU);
}

static void on_type_changed(lv_event_t *e)
{
    (void)e;
    // Auto-update the name field with the default name for the chosen type
    char def_name[SESSION_NAME_MAX];
    session_build_default_name(selected_type(), def_name, sizeof(def_name));
    lv_textarea_set_text(s_ta_name, def_name);
    update_vax_visibility();
}

static lv_obj_t *s_kb_ta = NULL;  // which textarea the keyboard is editing

// Restore all body widgets to their normal positions within s_content.
static void restore_layout(void)
{
    s_kb_ta = NULL;
    // Type row
    lv_obj_clear_flag(s_lbl_type_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_lbl_type_label, 12, ROW_TYPE_LBL_Y);
    lv_obj_clear_flag(s_dd_type,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_dd_type, 78, ROW_TYPE_DD_Y);
    lv_obj_set_size(s_dd_type, 390, 42);
    update_vax_visibility();
    // Name row
    lv_obj_clear_flag(s_lbl_name_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_lbl_name_label, 12, ROW_NAME_LBL_Y);
    lv_obj_clear_flag(s_ta_name,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_ta_name, 70, ROW_NAME_TA_Y);
    lv_obj_set_size(s_ta_name, 398, 44);
    // Note row
    lv_obj_clear_flag(s_lbl_note_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_lbl_note_label, 12, ROW_NOTE_LBL_Y);
    lv_obj_clear_flag(s_ta_note,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_ta_note, 70, ROW_NOTE_TA_Y);
    lv_obj_set_size(s_ta_note, 398, 44);
    // Keyboard + scroll
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
}

static void show_keyboard(lv_obj_t *ta, lv_obj_t *lbl)
{
    s_kb_ta = ta;
    // Scroll content to top so the focused textarea appears above the keyboard.
    lv_obj_scroll_to_y(s_content, 0, LV_ANIM_OFF);
    // Hide all body widgets
    lv_obj_add_flag(s_lbl_type_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dd_type,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_vax_section,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_name_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ta_name,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_note_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ta_note,        LV_OBJ_FLAG_HIDDEN);
    // Show only the focused row at top of s_content (below the 40 px header).
    // s_kb is on s_scr at ALIGN_BOTTOM_MID → top at y=120.
    // s_content starts at y=40 on screen, so ta at content-y=28 → screen-y=68. Fits.
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(lbl, 12, 6);
    lv_obj_clear_flag(ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ta, 12, 28);
    lv_obj_set_size(ta, 456, 44);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, ta);
}

static void on_name_focused(lv_event_t *e)
{
    (void)e;
    if (!lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) return;
    show_keyboard(s_ta_name, s_lbl_name_label);
}

static void on_name_clicked(lv_event_t *e)
{
    (void)e;
    if (lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) {
        show_keyboard(s_ta_name, s_lbl_name_label);
    }
}

static void on_note_focused(lv_event_t *e)
{
    (void)e;
    if (!lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) return;
    show_keyboard(s_ta_note, s_lbl_note_label);
}

static void on_note_clicked(lv_event_t *e)
{
    (void)e;
    if (lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) {
        show_keyboard(s_ta_note, s_lbl_note_label);
    }
}

static void on_kb_ready(lv_event_t *e)
{
    (void)e;
    restore_layout();
}

static void on_create(lv_event_t *e)
{
    (void)e;

    // Dismiss keyboard if open (textarea values are still committed).
    if (!lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }

    const char *name = lv_textarea_get_text(s_ta_name);
    session_type_t type = selected_type();

    // Collect selected vaccines
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

    uint32_t new_id = 0;
    esp_err_t err = session_create(type, name[0] ? name : NULL,
                                   vax_count ? vax_ids : NULL, vax_count, &new_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Created session %" PRIu32, new_id);
        const char *note = lv_textarea_get_text(s_ta_note);
        if (note && note[0]) session_save_note(new_id, note);
        ui_manager_show(SCREEN_SCAN);
    } else {
        ESP_LOGE(TAG, "session_create failed: %s", esp_err_to_name(err));
    }
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;

    // Restore layout in case user navigated away while keyboard was open.
    restore_layout();

    lv_textarea_set_text(s_ta_note, "");

    // Reset type to General so the screen is fresh each visit
    lv_dropdown_set_selected(s_dd_type, 0);

    // Pre-fill name with default for current type selection
    char def_name[SESSION_NAME_MAX];
    session_build_default_name(selected_type(), def_name, sizeof(def_name));
    lv_textarea_set_text(s_ta_name, def_name);

    // Populate vaccines list
    populate_vaccines();
    update_vax_visibility();
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_new_create(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header (fixed — not inside s_content) ─────────────────────────────────
    s_hdr = lv_obj_create(s_scr);
    lv_obj_set_size(s_hdr, 480, 40);
    lv_obj_set_pos(s_hdr, 0, 0);
    lv_obj_clear_flag(s_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_hdr, 0, LV_PART_MAIN);

    // Back button (left)
    lv_obj_t *btn_back = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_back, 60, 32);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_back, 10);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    s_lbl_back = lv_label_create(btn_back);
    lv_label_set_text(s_lbl_back, i18n_t(STR_BTN_BACK));
    lv_obj_set_style_text_font(s_lbl_back, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_back);

    // Title (center)
    s_lbl_title = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_NEW));
    lv_obj_set_style_text_font(s_lbl_title, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // Create button (right)
    lv_obj_t *btn_create = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_create, 80, 32);
    lv_obj_align(btn_create, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_set_style_radius(btn_create, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_create, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_create, 10);
    lv_obj_add_event_cb(btn_create, on_create, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_create = lv_label_create(btn_create);
    lv_label_set_text(s_lbl_btn_create, i18n_t(STR_SESSION_CREATE));
    lv_obj_set_style_text_font(s_lbl_btn_create, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_create);

    // ── Scrollable content area (below header) ────────────────────────────────
    s_content = lv_obj_create(s_scr);
    lv_obj_set_size(s_content, 480, 280);
    lv_obj_set_pos(s_content, 0, 40);
    lv_obj_set_style_border_width(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_content, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(s_content, LV_DIR_VER);

    // ── Type row ──────────────────────────────────────────────────────────────
    s_lbl_type_label = lv_label_create(s_content);
    lv_label_set_text(s_lbl_type_label, i18n_t(STR_SESSION_TYPE));
    lv_obj_set_style_text_font(s_lbl_type_label, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_type_label, 12, ROW_TYPE_LBL_Y);

    s_dd_type = lv_dropdown_create(s_content);
    lv_obj_set_size(s_dd_type, 390, 42);
    lv_obj_set_pos(s_dd_type, 78, ROW_TYPE_DD_Y);
    lv_obj_set_style_text_font(s_dd_type, &pilocows_font_20, LV_PART_MAIN);
    char type_opts[256];
    build_type_options(type_opts, sizeof(type_opts));
    lv_dropdown_set_options(s_dd_type, type_opts);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_dd_type), &pilocows_font_20, LV_PART_MAIN);
    lv_obj_add_event_cb(s_dd_type, on_type_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Name row ──────────────────────────────────────────────────────────────
    s_lbl_name_label = lv_label_create(s_content);
    lv_label_set_text(s_lbl_name_label, i18n_t(STR_SESSION_NAME));
    lv_obj_set_style_text_font(s_lbl_name_label, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_name_label, 12, ROW_NAME_LBL_Y);

    s_ta_name = lv_textarea_create(s_content);
    lv_obj_set_size(s_ta_name, 398, 44);
    lv_obj_set_pos(s_ta_name, 70, ROW_NAME_TA_Y);
    lv_obj_set_style_text_font(s_ta_name, &pilocows_font_20, LV_PART_MAIN);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_max_length(s_ta_name, SESSION_NAME_MAX - 1);
    lv_obj_set_scrollbar_mode(s_ta_name, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_name, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ta_name, on_name_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_name, on_name_clicked, LV_EVENT_CLICKED, NULL);

    // ── Note row ──────────────────────────────────────────────────────────────
    s_lbl_note_label = lv_label_create(s_content);
    lv_label_set_text(s_lbl_note_label, i18n_t(STR_NOTE));
    lv_obj_set_style_text_font(s_lbl_note_label, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_note_label, 12, ROW_NOTE_LBL_Y);

    s_ta_note = lv_textarea_create(s_content);
    lv_obj_set_size(s_ta_note, 398, 44);
    lv_obj_set_pos(s_ta_note, 70, ROW_NOTE_TA_Y);
    lv_obj_set_style_text_font(s_ta_note, &pilocows_font_20, LV_PART_MAIN);
    lv_textarea_set_one_line(s_ta_note, true);
    lv_textarea_set_max_length(s_ta_note, SESSION_NOTE_MAX - 1);
    lv_textarea_set_placeholder_text(s_ta_note, i18n_t(STR_SESSION_NOTE_PLACEHOLDER));
    lv_obj_set_scrollbar_mode(s_ta_note, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_note, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ta_note, on_note_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_note, on_note_clicked, LV_EVENT_CLICKED, NULL);

    // ── Vaccine section ────────────────────────────────────────────────────────
    // Height is computed dynamically in populate_vaccines() so s_content scrolls.
    s_vax_section = lv_obj_create(s_content);
    lv_obj_set_size(s_vax_section, 456, 40);
    lv_obj_set_pos(s_vax_section, 12, ROW_VAX_Y);
    lv_obj_set_style_border_width(s_vax_section, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vax_section, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_vax_section, 6, LV_PART_MAIN);

    s_lbl_vax = lv_label_create(s_vax_section);
    lv_label_set_text(s_lbl_vax, i18n_t(STR_SESSION_SELECT_VAX));
    lv_obj_set_style_text_font(s_lbl_vax, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax, 0, 0);

    lv_obj_add_flag(s_vax_section, LV_OBJ_FLAG_HIDDEN);

    // ── Keyboard (on s_scr so it overlays s_content; hidden initially) ─────────
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, 480, 200);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb, &pilocows_font_22, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_READY,  NULL);
    lv_obj_add_event_cb(s_kb, on_kb_ready, LV_EVENT_CANCEL, NULL);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    memset(s_vax_cbs, 0, sizeof(s_vax_cbs));

    ESP_LOGI(TAG, "Session new screen created");
}

// ─────────────────────────────────────────────────────────────────────────────
// Load / refresh
// ─────────────────────────────────────────────────────────────────────────────

void screen_session_new_load(void)
{
    lv_scr_load(s_scr);
}

void screen_session_new_refresh_language(void)
{
    lv_label_set_text(s_lbl_title,       i18n_t(STR_SESSION_NEW));
    lv_label_set_text(s_lbl_back,        i18n_t(STR_BTN_BACK));
    lv_label_set_text(s_lbl_type_label,  i18n_t(STR_SESSION_TYPE));
    lv_label_set_text(s_lbl_name_label,  i18n_t(STR_SESSION_NAME));
    lv_label_set_text(s_lbl_note_label,  i18n_t(STR_NOTE));
    lv_label_set_text(s_lbl_btn_create,  i18n_t(STR_SESSION_CREATE));
    lv_label_set_text(s_lbl_vax,         i18n_t(STR_SESSION_SELECT_VAX));

    // Rebuild dropdown options in the new language
    char type_opts[256];
    build_type_options(type_opts, sizeof(type_opts));
    lv_dropdown_set_options(s_dd_type, type_opts);
}
