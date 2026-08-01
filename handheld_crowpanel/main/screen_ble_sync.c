#include "screen_ble_sync.h"
#include "app_fonts.h"
#include "ui_manager.h"
#include "i18n.h"
#include "strings_en.h"
#include "ble_gatt_server.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ── Full screen (480x800), header + status + session list ──────────────────

static lv_obj_t *s_scr = NULL;
static lv_obj_t *s_lbl_title = NULL;
static lv_obj_t *s_lbl_status = NULL;
static lv_obj_t *s_list = NULL;
static uint32_t s_reading_sid = 0;

typedef struct {
    ble_sync_status_t status;
    uint32_t session_id;
    char detail[68];
} ble_ui_update_t;

static void apply_ble_update(void *data) {
    ble_ui_update_t *u = (ble_ui_update_t *)data;

    if (!s_lbl_status || !s_list) { free(u); return; }

    switch (u->status) {
        case BLE_SYNC_CONNECTED:
            lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_CONNECTED));
            break;

        case BLE_SYNC_SESSION_READING: {
            lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_SYNCING));
            if (u->session_id != s_reading_sid) {
                s_reading_sid = u->session_id;
                char buf[80];
                snprintf(buf, sizeof(buf), "%s...", u->detail[0] ? u->detail : "?");
                lv_obj_t *btn = lv_list_add_btn(s_list, NULL, buf);
                lv_obj_set_style_text_font(btn, &lv_font_app_20, LV_PART_MAIN);
                lv_obj_set_style_text_color(btn, lv_color_hex(0x555555), LV_PART_MAIN);
            }
            break;
        }

        case BLE_SYNC_SESSION_DONE: {
            s_reading_sid = 0;
            uint32_t n = lv_obj_get_child_cnt(s_list);
            if (n > 0) {
                lv_obj_t *last = lv_obj_get_child(s_list, (int32_t)(n - 1));
                lv_obj_t *lbl = lv_obj_get_child(last, 0);
                if (lbl) {
                    char buf[80];
                    snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s", u->detail[0] ? u->detail : "?");
                    lv_label_set_text(lbl, buf);
                    lv_obj_set_style_text_color(lbl, lv_color_hex(0x27AE60), LV_PART_MAIN);
                }
            }
            lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_SYNC_DONE));
            break;
        }

        case BLE_SYNC_DISCONNECTED:
            lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_ADVERTISING));
            break;

        default:
            break;
    }

    free(u);
}

static void ble_status_cb(ble_sync_status_t status, uint32_t session_id, const char *detail) {
    ble_ui_update_t *u = (ble_ui_update_t *)malloc(sizeof(ble_ui_update_t));
    if (!u) return;
    u->status = status;
    u->session_id = session_id;
    strncpy(u->detail, detail ? detail : "", sizeof(u->detail) - 1);
    u->detail[sizeof(u->detail) - 1] = '\0';
    lv_async_call(apply_ble_update, u);
}

static void on_back(lv_event_t *e) {
    (void)e;
    ble_gatt_server_set_status_cb(NULL);
    ble_gatt_server_stop_advertising();
    s_reading_sid = 0;
    ui_manager_show(SCREEN_SETTINGS);
}

static void on_screen_loaded(lv_event_t *e) {
    (void)e;
    lv_obj_clean(s_list);
    s_reading_sid = 0;
    lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_ADVERTISING));

    ble_gatt_server_set_status_cb(ble_status_cb);
    ble_gatt_server_start_advertising();
    if (ble_gatt_server_is_connected()) {
        lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_CONNECTED));
    }
}

void screen_ble_sync_refresh_language(void) {
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_SYNC));
}

void screen_ble_sync_create(void) {
    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_text_font(s_scr, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_scr, on_screen_loaded, LV_EVENT_SCREEN_LOADED, NULL);

    // ── Header (80px tall, standard dark navy w/ back "<") ───────────────────
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

    s_lbl_title = lv_label_create(hdr);
    lv_label_set_text(s_lbl_title, i18n_t(STR_SETTINGS_SYNC));
    lv_obj_set_style_text_font(s_lbl_title, &lv_font_app_30, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_lbl_title, LV_ALIGN_CENTER, 0, 0);

    // ── Status line ───────────────────────────────────────────────────────────
    s_lbl_status = lv_label_create(s_scr);
    lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_ADVERTISING));
    lv_obj_set_style_text_font(s_lbl_status, &lv_font_app_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x2C3E50), LV_PART_MAIN);
    lv_obj_set_width(s_lbl_status, 440);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_status, 20, 100);

    // ── Session list ──────────────────────────────────────────────────────────
    s_list = lv_list_create(s_scr);
    lv_obj_set_size(s_list, 440, 560);
    lv_obj_set_pos(s_list, 20, 160);
    lv_obj_set_style_border_width(s_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_list, lv_color_hex(0xDDE1E7), LV_PART_MAIN);
    lv_obj_set_style_radius(s_list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 3, LV_PART_MAIN);
}

void screen_ble_sync_load(void) {
    lv_scr_load(s_scr);
}
