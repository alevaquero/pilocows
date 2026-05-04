#include "ui_popup.h"
#include "lvgl.h"

// ── Popup geometry ────────────────────────────────────────────────────────────
// Centered on 480×320. Box: 440×160 at (20, 80).
#define POPUP_W  440
#define POPUP_H  160
#define POPUP_X   20
#define POPUP_Y   80

static void on_ok(lv_event_t *e)
{
    lv_obj_t *popup = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_del(popup);
}

static void popup_show(const char *msg, lv_color_t bg_color, lv_color_t text_color)
{
    lv_obj_t *scr = lv_scr_act();

    // Semi-transparent full-screen blocker so touches don't reach content below
    lv_obj_t *overlay = lv_obj_create(scr);
    lv_obj_set_size(overlay, 480, 320);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_40, LV_PART_MAIN);
    lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(overlay, 0, LV_PART_MAIN);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Popup box (child of overlay so it moves/deletes together)
    lv_obj_t *box = lv_obj_create(overlay);
    lv_obj_set_size(box, POPUP_W, POPUP_H);
    lv_obj_set_pos(box, POPUP_X - 0, POPUP_Y - 0);  // relative to overlay
    lv_obj_set_style_bg_color(box, bg_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(box, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(box, 14, LV_PART_MAIN);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Message label
    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, msg);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, POPUP_W - 28);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, text_color, LV_PART_MAIN);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);

    // OK button
    lv_obj_t *btn = lv_btn_create(box);
    lv_obj_set_size(btn, POPUP_W - 28, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_set_ext_click_area(btn, 6);
    lv_obj_add_event_cb(btn, on_ok, LV_EVENT_CLICKED, overlay);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "OK");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_lbl, bg_color, LV_PART_MAIN);
    lv_obj_center(btn_lbl);
}

void ui_popup_error(const char *msg)
{
    popup_show(msg, lv_color_hex(0xC0392B), lv_color_white());
}

void ui_popup_info(const char *msg)
{
    popup_show(msg, lv_color_hex(0x2C3E50), lv_color_white());
}
