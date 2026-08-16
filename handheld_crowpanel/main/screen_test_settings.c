#include "screen_test_settings.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "ui_text_entry.h"
#include "session_storage.h"
#include "ui_icons.h"
#include "i18n.h"
#include "strings_en.h"
#include "lvgl.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "scr_test";

// ── Static label refs ─────────────────────────────────────────────────────────
static lv_obj_t *s_lbl_title;
static lv_obj_t *s_lbl_empty;

// ── List ──────────────────────────────────────────────────────────────────────
static lv_obj_t *s_list;

// ── Delete confirm overlay ────────────────────────────────────────────────────
static lv_obj_t *s_del_panel;
static lv_obj_t *s_lbl_del_msg;
static lv_obj_t *s_lbl_del_confirm;
static lv_obj_t *s_lbl_del_cancel;
static uint8_t s_del_pending_id = 0;

static lv_obj_t *s_hdr = NULL;
static lv_obj_t *s_scr = NULL;

static uint8_t s_id_buf[TEST_LIST_MAX];

static void on_row_delete(lv_event_t *e) {
    uint8_t id = *(uint8_t *)lv_event_get_user_data(e);
    s_del_pending_id = id;
    lv_obj_clear_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
}

static void rebuild_list(void) {
    lv_obj_clean(s_list);

    test_cfg_t tlist[TEST_LIST_MAX];
    int count = (int)test_list(tlist, TEST_LIST_MAX);

    if (count == 0) {
        lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < count; i++) {
        s_id_buf[i] = tlist[i].id;

        lv_obj_t *row = lv_list_add_btn(s_list, NULL, "");
        lv_obj_set_height(row, 72);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
        lv_obj_set_layout(row, 0);
        lv_obj_remove_event_cb(row, NULL);

        lv_obj_t *lbl = lv_label_create(row);
        lv_label_set_text(lbl, tlist[i].name);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, 300);
        lv_obj_set_style_text_font(lbl, &lv_font_app_28, LV_PART_MAIN);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 7, 0);

        lv_obj_t *btn_del = lv_btn_create(row);
        lv_obj_set_size(btn_del, 54, 54);
        lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, -7, 0);
        lv_obj_set_style_bg_opa(btn_del, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(btn_del, LV_OPA_20, LV_STATE_PRESSED | LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn_del, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn_del, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(btn_del, 0, LV_PART_MAIN);
        lv_obj_set_ext_click_area(btn_del, 6);

        ui_icon_create(btn_del, UI_SYMBOL_TRASH, lv_palette_main(LV_PALETTE_RED), &lv_font_app_36);

        lv_obj_add_event_cb(btn_del, on_row_delete, LV_EVENT_CLICKED, &s_id_buf[i]);
    }
}

static void on_back(lv_event_t *e) { (void)e; ui_manager_show(SCREEN_SETTINGS); }

static void on_add_confirm(const char *text, void *user_data) {
    (void)user_data;
    if (text && text[0]) {
        uint8_t new_id = 0;
        esp_err_t err = test_add(text, &new_id);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "test_add failed: %s", esp_err_to_name(err));
        }
    }
    rebuild_list();
}

static void on_add_btn(lv_event_t *e) {
    (void)e;
    ui_text_entry_cfg_t cfg = {
        .label = i18n_t(STR_TEST_NAME),
        .initial_text = "",
        .placeholder = NULL,
        .multiline = false,
        .password = false,
        .max_length = TEST_NAME_MAX - 1,
        .on_confirm = on_add_confirm,
        .on_cancel = NULL,
        .user_data = NULL,
    };
    ui_text_entry_show(&cfg);
}

static void on_del_confirm(lv_event_t *e) {
    (void)e;
    if (s_del_pending_id != 0) {
        test_delete(s_del_pending_id);
        s_del_pending_id = 0;
    }
    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
    rebuild_list();
}

static void on_del_cancel(lv_event_t *e) {
    (void)e;
    s_del_pending_id = 0;
    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);
    rebuild_list();
}

void screen_test_settings_create(void) {
    s_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header ──────────────────────────────────────────────────────────────
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
    lv_label_set_text(s_lbl_title, i18n_t(STR_TESTS_TITLE));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *btn_add = lv_btn_create(s_hdr);
    lv_obj_set_size(btn_add, 70, 70);
    lv_obj_align(btn_add, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_border_width(btn_add, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_add, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_add, 0, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_add, 6);
    lv_obj_add_event_cb(btn_add, on_add_btn, LV_EVENT_CLICKED, NULL);
    ui_icon_create(btn_add, UI_SYMBOL_PLUS, lv_color_white(), &lv_font_app_30);

    // ── Empty label ───────────────────────────────────────────────────────────
    s_lbl_empty = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_empty, i18n_t(STR_TEST_NONE));
    lv_obj_set_style_text_font(s_lbl_empty, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_empty, 0, 225);
    lv_obj_set_width(s_lbl_empty, 480);
    lv_obj_set_style_text_align(s_lbl_empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    // ── Test list ─────────────────────────────────────────────────────────────
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, 480, 720);
    lv_obj_set_pos(s_list, 0, 80);
    lv_obj_set_style_border_width(s_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 6, LV_PART_MAIN);

    // ── Delete confirm overlay ────────────────────────────────────────────────
    s_del_panel = lv_obj_create(s_scr);
    lv_obj_set_size(s_del_panel, 420, 200);
    lv_obj_align(s_del_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_clear_flag(s_del_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_color(s_del_panel, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_del_panel, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(s_del_panel, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_del_panel, 18, LV_PART_MAIN);

    s_lbl_del_msg = lv_label_create(s_del_panel);
    lv_label_set_text(s_lbl_del_msg, i18n_t(STR_TEST_CONFIRM_DELETE));
    lv_obj_set_style_text_font(s_lbl_del_msg, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_width(s_lbl_del_msg, 384);
    lv_obj_set_style_text_align(s_lbl_del_msg, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(s_lbl_del_msg, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *btn_del_ok = lv_btn_create(s_del_panel);
    lv_obj_set_size(btn_del_ok, 186, 54);
    lv_obj_align(btn_del_ok, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(btn_del_ok, lv_color_hex(0xC0392B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_del_ok, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_del_ok, 10);
    lv_obj_add_event_cb(btn_del_ok, on_del_confirm, LV_EVENT_CLICKED, NULL);
    s_lbl_del_confirm = lv_label_create(btn_del_ok);
    lv_label_set_text(s_lbl_del_confirm, i18n_t(STR_TEST_DELETE));
    lv_obj_set_style_text_color(s_lbl_del_confirm, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_del_confirm, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(s_lbl_del_confirm);

    lv_obj_t *btn_del_no = lv_btn_create(s_del_panel);
    lv_obj_set_size(btn_del_no, 186, 54);
    lv_obj_align(btn_del_no, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_border_width(btn_del_no, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_del_no, 4, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn_del_no, 10);
    lv_obj_add_event_cb(btn_del_no, on_del_cancel, LV_EVENT_CLICKED, NULL);
    s_lbl_del_cancel = lv_label_create(btn_del_no);
    lv_label_set_text(s_lbl_del_cancel, i18n_t(STR_BTN_CANCEL));
    lv_obj_set_style_text_font(s_lbl_del_cancel, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_center(s_lbl_del_cancel);

    lv_obj_add_flag(s_del_panel, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "Test settings screen created");
}

void screen_test_settings_load(void) {
    lv_scr_load(s_scr);
}

void screen_test_settings_refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(STR_TESTS_TITLE));
    lv_label_set_text(s_lbl_empty, i18n_t(STR_TEST_NONE));
    lv_label_set_text(s_lbl_del_msg, i18n_t(STR_TEST_CONFIRM_DELETE));
    lv_label_set_text(s_lbl_del_confirm, i18n_t(STR_TEST_DELETE));
    lv_label_set_text(s_lbl_del_cancel, i18n_t(STR_BTN_CANCEL));
    rebuild_list();
}
