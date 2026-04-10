#include "screen_settings.h"
#include "ui_manager.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "peripherals/buzzer.h"
#include "peripherals/vibrator.h"
#include "display/display.h"
#include "lvgl.h"

static lv_obj_t *s_screen = NULL;

static void on_back(lv_event_t *e)
{
    ui_manager_show(SCREEN_SCAN);
}

static void on_language(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool en = lv_obj_has_state(sw, LV_STATE_CHECKED);
    i18n_set_language(en ? LANG_EN : LANG_ES);
    // Reload scan screen to apply new language
    // (Full re-render would require recreating all labels — simplified here)
}

static void on_buzzer(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    buzzer_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_vibrator(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    vibrator_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void on_brightness(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int val = lv_slider_get_value(slider);
    display_set_brightness((uint8_t)val);
}

void screen_settings_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x1a1a2e), LV_PART_MAIN);

    // Title
    lv_obj_t *lbl_title = lv_label_create(s_screen);
    lv_label_set_text(lbl_title, i18n_t(STR_SETTINGS_TITLE));
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0xeeeeee), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, LV_PART_MAIN);

    // Back button
    lv_obj_t *btn_back = lv_btn_create(s_screen);
    lv_obj_set_size(btn_back, 70, 30);
    lv_obj_align(btn_back, LV_ALIGN_TOP_LEFT, 10, 7);
    lv_obj_add_event_cb(btn_back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, i18n_t(STR_BTN_BACK));
    lv_obj_center(lbl_back);

    int row_y = 55;
    int row_h = 45;

    // Helper macro for a settings row: label on left, switch on right
    #define SETTINGS_ROW(label_text, callback)                              \
    {                                                                       \
        lv_obj_t *lbl = lv_label_create(s_screen);                         \
        lv_label_set_text(lbl, label_text);                                 \
        lv_obj_set_pos(lbl, 20, row_y + 10);                               \
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xdddddd), LV_PART_MAIN); \
        lv_obj_t *sw = lv_switch_create(s_screen);                         \
        lv_obj_set_pos(sw, 380, row_y + 8);                                \
        lv_obj_add_state(sw, LV_STATE_CHECKED);                            \
        lv_obj_add_event_cb(sw, callback, LV_EVENT_VALUE_CHANGED, NULL);   \
        row_y += row_h;                                                     \
    }

    // Language row — toggle switch EN/ES
    {
        lv_obj_t *lbl = lv_label_create(s_screen);
        lv_label_set_text(lbl, i18n_t(STR_SETTINGS_LANGUAGE));
        lv_obj_set_pos(lbl, 20, row_y + 10);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xdddddd), LV_PART_MAIN);
        lv_obj_t *sw = lv_switch_create(s_screen);
        lv_obj_set_pos(sw, 380, row_y + 8);
        if (i18n_get_language() == LANG_EN) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, on_language, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_t *lbl_en = lv_label_create(s_screen);
        lv_label_set_text(lbl_en, "ES | EN");
        lv_obj_set_pos(lbl_en, 310, row_y + 13);
        lv_obj_set_style_text_color(lbl_en, lv_color_hex(0xaaaaaa), LV_PART_MAIN);
        row_y += row_h;
    }

    SETTINGS_ROW(i18n_t(STR_SETTINGS_BUZZER),   on_buzzer)
    SETTINGS_ROW(i18n_t(STR_SETTINGS_VIBRATOR), on_vibrator)

    // Brightness slider
    {
        lv_obj_t *lbl = lv_label_create(s_screen);
        lv_label_set_text(lbl, i18n_t(STR_SETTINGS_BRIGHTNESS));
        lv_obj_set_pos(lbl, 20, row_y + 10);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xdddddd), LV_PART_MAIN);
        lv_obj_t *slider = lv_slider_create(s_screen);
        lv_obj_set_size(slider, 200, 20);
        lv_obj_set_pos(slider, 160, row_y + 12);
        lv_slider_set_range(slider, 20, 100);
        lv_slider_set_value(slider, 80, LV_ANIM_OFF);
        lv_obj_add_event_cb(slider, on_brightness, LV_EVENT_VALUE_CHANGED, NULL);
        row_y += row_h;
    }

    // Version label
    {
        lv_obj_t *lbl = lv_label_create(s_screen);
        lv_label_set_text(lbl, "Pilocows v0.1.0");
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x666666), LV_PART_MAIN);
    }
}

void screen_settings_load(void)
{
    lv_scr_load(s_screen);
}
