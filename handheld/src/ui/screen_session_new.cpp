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

static const char *TAG = "scr_sess_new";

// ── Header (hidden while keyboard is visible) ─────────────────────────────────
static lv_obj_t *s_hdr;

// ── Static label refs ─────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_back;
static lv_obj_t *s_lbl_type_label;
static lv_obj_t *s_lbl_name_label;
static lv_obj_t *s_lbl_btn_create;

// ── Widgets ───────────────────────────────────────────────────────────────────
static lv_obj_t *s_dd_type;      // dropdown for session type
static lv_obj_t *s_ta_name;      // text area for session name
static lv_obj_t *s_kb;           // keyboard (shown on name focus)
static lv_obj_t *s_btn_create;

// ── Vaccine checkboxes ────────────────────────────────────────────────────────
static lv_obj_t *s_vax_section;  // container, hidden unless type=VACCINATION
static lv_obj_t *s_lbl_vax;
static lv_obj_t *s_vax_cbs[VACCINE_LIST_MAX];
static uint8_t   s_vax_ids[VACCINE_LIST_MAX];
static int       s_vax_count = 0;

static lv_obj_t *s_scr = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

// Build the dropdown option string for session types.
// lv_dropdown expects "\n"-separated options.
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
        lv_obj_set_style_text_font(s_vax_cbs[i], &lv_font_montserrat_18, LV_PART_MAIN);
        // Start below the "Select vaccines" label (~22px) + 4px gap
        lv_obj_set_pos(s_vax_cbs[i], 0, 26 + i * 30);
    }
    // Height is fixed; scrolling activates automatically if content overflows
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

static void show_keyboard(void)
{
    lv_obj_add_flag(s_hdr,            LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_type_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_dd_type,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_vax_section,    LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_create,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_lbl_name_label, 12, 6);
    lv_obj_set_pos(s_ta_name, 12, 28);
    lv_obj_clear_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, s_ta_name);
}

static void on_name_focused(lv_event_t *e)
{
    (void)e;
    show_keyboard();
}

static void on_name_clicked(lv_event_t *e)
{
    (void)e;
    // Re-open keyboard if it was dismissed but textarea still has focus
    if (lv_obj_has_flag(s_kb, LV_OBJ_FLAG_HIDDEN)) {
        show_keyboard();
    }
}

static void on_kb_ready(lv_event_t *e)
{
    (void)e;
    // Restore all content — name row back to inline position
    lv_obj_clear_flag(s_hdr,            LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_type_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_dd_type,        LV_OBJ_FLAG_HIDDEN);
    update_vax_visibility();
    lv_obj_clear_flag(s_btn_create,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_lbl_name_label, 12, 109);
    lv_obj_set_pos(s_ta_name, 70, 100);
    lv_obj_set_size(s_ta_name, 398, 50);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
}

static void on_create(lv_event_t *e)
{
    (void)e;

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
        ui_manager_show(SCREEN_SCAN);
    } else {
        ESP_LOGE(TAG, "session_create failed: %s", esp_err_to_name(err));
    }
}

static void on_screen_loaded(lv_event_t *e)
{
    (void)e;

    // Always restore layout in case user navigated away while keyboard was open
    lv_obj_clear_flag(s_hdr,            LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_type_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_dd_type,        LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_btn_create,     LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(s_lbl_name_label, 12, 109);
    lv_obj_set_pos(s_ta_name, 70, 100);
    lv_obj_set_size(s_ta_name, 398, 50);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

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

    // ── Header ────────────────────────────────────────────────────────────────
    s_hdr = lv_obj_create(s_scr);
    lv_obj_set_size(s_hdr, 480, 40);
    lv_obj_set_pos(s_hdr, 0, 0);
    lv_obj_clear_flag(s_hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_hdr, 0, LV_PART_MAIN);

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
    lv_obj_set_style_text_font(s_lbl_back, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(s_lbl_back);

    s_lbl_title = lv_label_create(s_hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SESSION_NEW));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Type row (y=48, h=44): "Type" label left + dropdown right ────────────
    // Label vertically centred: y = 48 + (44-22)/2 = 59
    s_lbl_type_label = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_type_label, i18n_t(STR_SESSION_TYPE));
    lv_obj_set_style_text_font(s_lbl_type_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_type_label, 12, 59);

    s_dd_type = lv_dropdown_create(s_scr);
    lv_obj_set_size(s_dd_type, 390, 42);
    lv_obj_set_pos(s_dd_type, 78, 50);
    lv_obj_set_style_text_font(s_dd_type, &lv_font_montserrat_20, LV_PART_MAIN);
    char type_opts[256];
    build_type_options(type_opts, sizeof(type_opts));
    lv_dropdown_set_options(s_dd_type, type_opts);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_dd_type), &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(s_dd_type, on_type_changed, LV_EVENT_VALUE_CHANGED, NULL);

    // ── Name row (y=100, h=44): "Name" label left + textarea right ───────────
    // Label vertically centred: y = 100 + (44-22)/2 = 111
    s_lbl_name_label = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_name_label, i18n_t(STR_SESSION_NAME));
    lv_obj_set_style_text_font(s_lbl_name_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_name_label, 12, 109);

    s_ta_name = lv_textarea_create(s_scr);
    lv_obj_set_size(s_ta_name, 398, 50);
    lv_obj_set_pos(s_ta_name, 70, 100);
    lv_obj_set_style_text_font(s_ta_name, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_max_length(s_ta_name, SESSION_NAME_MAX - 1);
    lv_obj_set_scrollbar_mode(s_ta_name, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_ta_name, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_ta_name, on_name_focused, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s_ta_name, on_name_clicked, LV_EVENT_CLICKED, NULL);

    // ── Vaccine section (y=152, h=122): scrolls if many vaccines ─────────────
    s_vax_section = lv_obj_create(s_scr);
    lv_obj_set_size(s_vax_section, 456, 122);
    lv_obj_set_pos(s_vax_section, 12, 152);
    lv_obj_set_style_border_width(s_vax_section, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vax_section, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_vax_section, 6, LV_PART_MAIN);

    s_lbl_vax = lv_label_create(s_vax_section);
    lv_label_set_text(s_lbl_vax, i18n_t(STR_SESSION_SELECT_VAX));
    lv_obj_set_style_text_font(s_lbl_vax, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_vax, 0, 0);

    lv_obj_add_flag(s_vax_section, LV_OBJ_FLAG_HIDDEN);

    // ── Create button (y=272, h=40) ───────────────────────────────────────────
    s_btn_create = lv_btn_create(s_scr);
    lv_obj_set_size(s_btn_create, 456, 40);
    lv_obj_set_pos(s_btn_create, 12, 272);
    lv_obj_set_style_radius(s_btn_create, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(s_btn_create, 10);
    lv_obj_add_event_cb(s_btn_create, on_create, LV_EVENT_CLICKED, NULL);
    s_lbl_btn_create = lv_label_create(s_btn_create);
    lv_label_set_text(s_lbl_btn_create, i18n_t(STR_SESSION_CREATE));
    lv_obj_set_style_text_font(s_lbl_btn_create, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_center(s_lbl_btn_create);

    // ── Keyboard (hidden initially) ───────────────────────────────────────────
    s_kb = lv_keyboard_create(s_scr);
    lv_obj_set_size(s_kb, 480, 200);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb, &lv_font_montserrat_22, LV_PART_ITEMS);
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
    lv_label_set_text(s_lbl_btn_create,  i18n_t(STR_SESSION_CREATE));
    lv_label_set_text(s_lbl_vax,         i18n_t(STR_SESSION_SELECT_VAX));

    // Rebuild dropdown options in the new language
    char type_opts[256];
    build_type_options(type_opts, sizeof(type_opts));
    lv_dropdown_set_options(s_dd_type, type_opts);
}
