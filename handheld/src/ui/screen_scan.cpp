#include "screen_scan.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Screen layout (480 x 320 landscape):
//
//  ┌─────────────────────────────────────────────────┐
//  │  [Pilocows]               [Ajustes / Settings]  │  ← Header bar
//  ├─────────────────────────────────────────────────┤
//  │                                                 │
//  │   Event: [Weighing ▼]          Scanned: 0       │
//  │                                                 │
//  │   ┌───────────────────────────────────────────┐ │
//  │   │      Ready to scan                        │ │
//  │   │      EID: ---                             │ │
//  │   └───────────────────────────────────────────┘ │
//  │                                                 │
//  │              [SCAN]       [Sync to PC]          │
//  │                                                 │
//  │   Status: ...                                   │
//  └─────────────────────────────────────────────────┘
// ---------------------------------------------------------------------------

static lv_obj_t *s_screen        = NULL;
static lv_obj_t *s_lbl_eid       = NULL;
static lv_obj_t *s_lbl_status    = NULL;
static lv_obj_t *s_lbl_count     = NULL;
static lv_obj_t *s_lbl_event     = NULL;
static lv_obj_t *s_btn_scan      = NULL;
static lv_obj_t *s_btn_sync      = NULL;
static lv_timer_t *s_status_timer = NULL;

static char s_current_event[32] = "general";

// Forward declarations for button callbacks
static void on_btn_scan(lv_event_t *e);
static void on_btn_sync(lv_event_t *e);
static void on_event_dropdown(lv_event_t *e);
static void clear_status_cb(lv_timer_t *t);

void screen_scan_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    // Header bar
    lv_obj_t *header = lv_obj_create(s_screen);
    lv_obj_set_size(header, 480, 40);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x16213e), LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN);

    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "Pilocows");
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x0f3460), LV_PART_MAIN);

    // Settings button in header
    lv_obj_t *btn_settings = lv_btn_create(header);
    lv_obj_set_size(btn_settings, 90, 28);
    lv_obj_align(btn_settings, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_t *lbl_settings = lv_label_create(btn_settings);
    lv_label_set_text(lbl_settings, i18n_t(STR_SCREEN_SETTINGS));
    lv_obj_center(lbl_settings);

    // Event type dropdown
    lv_obj_t *lbl_event_title = lv_label_create(s_screen);
    lv_label_set_text(lbl_event_title, i18n_t(STR_SCAN_EVENT_LABEL));
    lv_obj_align(lbl_event_title, LV_ALIGN_TOP_LEFT, 15, 55);
    lv_obj_set_style_text_color(lbl_event_title, lv_color_hex(0xeeeeee), LV_PART_MAIN);

    lv_obj_t *dd_event = lv_dropdown_create(s_screen);
    lv_dropdown_set_options(dd_event,
        "General\n"
        "Weighing / Pesaje\n"
        "Vaccination / Vacunacion\n"
        "Pregnancy / Prenez\n"
        "TB Test / Tuberculosis\n"
        "Removal / Baja"
    );
    lv_obj_set_size(dd_event, 220, 35);
    lv_obj_align(dd_event, LV_ALIGN_TOP_LEFT, 110, 50);
    lv_obj_add_event_cb(dd_event, on_event_dropdown, LV_EVENT_VALUE_CHANGED, NULL);

    // Scan count label
    s_lbl_count = lv_label_create(s_screen);
    lv_label_set_text(s_lbl_count, "Scanned: 0");
    lv_obj_align(s_lbl_count, LV_ALIGN_TOP_RIGHT, -15, 55);
    lv_obj_set_style_text_color(s_lbl_count, lv_color_hex(0xeeeeee), LV_PART_MAIN);

    // EID display card
    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_size(card, 440, 100);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x16213e), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);

    s_lbl_status = lv_label_create(card);
    lv_label_set_text(s_lbl_status, i18n_t(STR_SCAN_READY));
    lv_obj_align(s_lbl_status, LV_ALIGN_TOP_LEFT, 10, 8);
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xaaaaaa), LV_PART_MAIN);

    s_lbl_eid = lv_label_create(card);
    lv_label_set_text(s_lbl_eid, "EID: ---");
    lv_obj_align(s_lbl_eid, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_text_color(s_lbl_eid, lv_color_hex(0xe94560), LV_PART_MAIN);
    lv_obj_set_style_text_font(s_lbl_eid, &lv_font_montserrat_20, LV_PART_MAIN);

    // Scan button
    s_btn_scan = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_scan, 160, 44);
    lv_obj_align(s_btn_scan, LV_ALIGN_BOTTOM_LEFT, 40, -15);
    lv_obj_set_style_bg_color(s_btn_scan, lv_color_hex(0xe94560), LV_PART_MAIN);
    lv_obj_add_event_cb(s_btn_scan, on_btn_scan, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_scan = lv_label_create(s_btn_scan);
    lv_label_set_text(lbl_scan, i18n_t(STR_SCREEN_SCAN));
    lv_obj_center(lbl_scan);

    // Sync button
    s_btn_sync = lv_btn_create(s_screen);
    lv_obj_set_size(s_btn_sync, 160, 44);
    lv_obj_align(s_btn_sync, LV_ALIGN_BOTTOM_RIGHT, -40, -15);
    lv_obj_set_style_bg_color(s_btn_sync, lv_color_hex(0x0f3460), LV_PART_MAIN);
    lv_obj_add_event_cb(s_btn_sync, on_btn_sync, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_sync = lv_label_create(s_btn_sync);
    lv_label_set_text(lbl_sync, i18n_t(STR_SCAN_SYNC));
    lv_obj_center(lbl_sync);
}

void screen_scan_load(void)
{
    lv_scr_load(s_screen);
}

void screen_scan_show_tag(const char *eid, const char *event_type)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "EID: %s", eid);
    lv_label_set_text(s_lbl_eid, buf);
    lv_label_set_text(s_lbl_status, i18n_t(STR_SCAN_TAG_FOUND));
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0x00b894), LV_PART_MAIN);
}

void screen_scan_update_count(uint32_t count)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%s %lu", i18n_t(STR_SCAN_COUNT_LABEL), (unsigned long)count);
    lv_label_set_text(s_lbl_count, buf);
}

void screen_scan_show_status(const char *msg)
{
    lv_label_set_text(s_lbl_status, msg);
    // Auto-clear after 2s
    if (s_status_timer) lv_timer_del(s_status_timer);
    s_status_timer = lv_timer_create(clear_status_cb, 2000, NULL);
    lv_timer_set_repeat_count(s_status_timer, 1);
}

void screen_scan_set_event_type(const char *event_type)
{
    strncpy(s_current_event, event_type, sizeof(s_current_event) - 1);
}

// ---------------------------------------------------------------------------
// Private callbacks
// ---------------------------------------------------------------------------
static void clear_status_cb(lv_timer_t *t)
{
    lv_label_set_text(s_lbl_status, i18n_t(STR_SCAN_READY));
    lv_obj_set_style_text_color(s_lbl_status, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
    s_status_timer = NULL;
}

static const char *s_event_codes[] = {
    SCAN_EVENT_GENERAL, SCAN_EVENT_WEIGHING, SCAN_EVENT_VACCINATION,
    SCAN_EVENT_PREGNANCY, SCAN_EVENT_TB_TEST, SCAN_EVENT_REMOVAL
};

static void on_event_dropdown(lv_event_t *e)
{
    lv_obj_t *dd = (lv_obj_t *)lv_event_get_target(e);
    uint16_t idx = lv_dropdown_get_selected(dd);
    if (idx < 6) {
        strncpy(s_current_event, s_event_codes[idx], sizeof(s_current_event) - 1);
    }
}

static void on_btn_scan(lv_event_t *e)
{
    // Scan button pressed via touchscreen — same effect as physical SCAN button.
    // The main app handles actual scanning; this just provides touch UI parity.
    // Raise an LVGL custom event that main.cpp can listen to.
    lv_event_send(lv_obj_get_parent(s_btn_scan), LV_EVENT_REFRESH, (void *)"scan");
}

static void on_btn_sync(lv_event_t *e)
{
    lv_event_send(lv_obj_get_parent(s_btn_sync), LV_EVENT_REFRESH, (void *)"sync");
}
