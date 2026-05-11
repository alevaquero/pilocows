#include "screen_ble_sync.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "ble/ble_server.h"
#include "lvgl.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "fonts.h"

// ---------------------------------------------------------------------------
// Modal overlay — created on demand on top of whatever screen is active.
// Layout (card 460×270, centred on 480×320):
//   y=8   title "Sync to PC"
//   y=44  status label (wraps, centred)
//   y=90  session list  (scrollable, h=142)
//   y=240 Close button  (centred)
// ---------------------------------------------------------------------------

static lv_obj_t *s_overlay    = NULL;   // dim backdrop + card parent
static lv_obj_t *s_lbl_status = NULL;   // live BLE state text
static lv_obj_t *s_list       = NULL;   // per-session log rows
static uint32_t  s_reading_sid = 0;     // session currently being read (0 = none)

// ---------------------------------------------------------------------------
// BLE status callback — runs in the NimBLE task; must not touch LVGL directly.
// ---------------------------------------------------------------------------

typedef struct {
    ble_sync_status_t status;
    uint32_t          session_id;
    char              detail[68];
} ble_ui_update_t;

static void apply_ble_update(void *data)
{
    ble_ui_update_t *u = (ble_ui_update_t *)data;

    // Modal may have been closed before this fires
    if (!s_lbl_status || !s_list) { free(u); return; }

    switch (u->status) {
    case BLE_SYNC_CONNECTED:
        lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_CONNECTED));
        break;

    case BLE_SYNC_SESSION_READING: {
        lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_SYNCING));
        // Only add ONE row per session — gatt_session_data_cb fires this event for
        // every BLE page read (every 5 records), so a 100-record session produces
        // 20 firings.  Adding 20 rows floods the list and locks up the LVGL task.
        if (u->session_id != s_reading_sid) {
            s_reading_sid = u->session_id;
            char buf[80];
            snprintf(buf, sizeof(buf), "%s...", u->detail[0] ? u->detail : "?");
            lv_obj_t *btn = lv_list_add_btn(s_list, NULL, buf);
            lv_obj_set_style_text_font(btn, &pilocows_font_14, LV_PART_MAIN);
            lv_obj_set_style_text_color(btn, lv_color_hex(0x555555), LV_PART_MAIN);
        }
        break;
    }

    case BLE_SYNC_SESSION_DONE: {
        s_reading_sid = 0;
        uint32_t n = lv_obj_get_child_cnt(s_list);
        if (n > 0) {
            lv_obj_t *last = lv_obj_get_child(s_list, (int32_t)(n - 1));
            lv_obj_t *lbl  = lv_obj_get_child(last, 0);
            if (lbl) {
                char buf[80];
                snprintf(buf, sizeof(buf), LV_SYMBOL_OK " %s",
                         u->detail[0] ? u->detail : "?");
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

static void ble_status_cb(ble_sync_status_t status, uint32_t session_id, const char *detail)
{
    ble_ui_update_t *u = (ble_ui_update_t *)malloc(sizeof(ble_ui_update_t));
    if (!u) return;
    u->status     = status;
    u->session_id = session_id;
    strlcpy(u->detail, detail ? detail : "", sizeof(u->detail));
    lv_async_call(apply_ble_update, u);
}

// ---------------------------------------------------------------------------
// Close
// ---------------------------------------------------------------------------

static void close_modal(void)
{
    ble_server_set_status_cb(NULL);
    ble_server_stop_advertising();  // disconnects PC and stops advertising entirely
    if (s_overlay) {
        lv_obj_del_async(s_overlay);
        s_overlay = s_lbl_status = s_list = NULL;
    }
    s_reading_sid = 0;
}

static void on_close(lv_event_t *e)
{
    (void)e;
    close_modal();
}

// ---------------------------------------------------------------------------
// Language refresh (called while modal may or may not be open)
// ---------------------------------------------------------------------------

void screen_ble_sync_refresh_language(void)
{
    // Nothing to do — modal is ephemeral; it will read the current language
    // the next time it is opened.
}

// ---------------------------------------------------------------------------
// Show modal (public API)
// ---------------------------------------------------------------------------

void screen_ble_sync_show_modal(void)
{
    // Dim overlay over the current screen
    s_overlay = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_overlay, 480, 320);
    lv_obj_set_pos(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Card
    lv_obj_t *card = lv_obj_create(s_overlay);
    lv_obj_set_size(card, 460, 270);
    lv_obj_center(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(card, 4, LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, i18n_t(STR_SETTINGS_SYNC));
    lv_obj_set_style_text_font(title, &pilocows_font_20, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    // Status label  (y=34, leaves room for title at y=6 ~24px tall)
    s_lbl_status = lv_label_create(card);
    lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_ADVERTISING));
    lv_obj_set_style_text_font(s_lbl_status, &pilocows_font_18, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x2C3E50), LV_PART_MAIN);
    lv_obj_set_width(s_lbl_status, 440);
    lv_label_set_long_mode(s_lbl_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_lbl_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(s_lbl_status, 4, 34);

    // Session log list  y=76, h=138 → bottom=214
    s_list = lv_list_create(card);
    lv_obj_set_size(s_list, 436, 138);
    lv_obj_set_pos(s_list, 4, 76);
    lv_obj_set_style_border_width(s_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_list, lv_color_hex(0xDDE1E7), LV_PART_MAIN);
    lv_obj_set_style_radius(s_list, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_list, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_list, 2, LV_PART_MAIN);

    // Close button  y=220, h=36 → bottom=256 < inner-height 262 ✓
    lv_obj_t *btn_close = lv_btn_create(card);
    lv_obj_set_size(btn_close, 130, 36);
    lv_obj_set_pos(btn_close, (452 - 130) / 2, 220);
    lv_obj_add_event_cb(btn_close, on_close, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_close = lv_label_create(btn_close);
    lv_label_set_text(lbl_close, i18n_t(STR_BTN_CLOSE));
    lv_obj_center(lbl_close);

    // Register BLE callback and start advertising
    ble_server_set_status_cb(ble_status_cb);
    ble_server_start_advertising();
    if (ble_server_is_connected()) {
        lv_label_set_text(s_lbl_status, i18n_t(STR_BLE_CONNECTED));
    }
}
