#include "screen_splash.h"
#include "img_splash_logo.h"
#include "app_fonts.h"
#include "app_version.h"
#include "lvgl.h"

// Matches the app icon's own background exactly (sampled from its corner
// pixel) so the logo image reads as full-bleed with no visible edge/box.
#define SPLASH_BG_COLOR 0x1E293B

void screen_splash_show(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(SPLASH_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, &img_splash_logo);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -40);

    lv_obj_t *spinner = lv_spinner_create(scr, 1000, 60);
    lv_obj_set_size(spinner, 40, 40);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, 140);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x3B4658), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t *lbl_version = lv_label_create(scr);
    lv_label_set_text_fmt(lbl_version, "v%s", FIRMWARE_VERSION);
    lv_obj_set_style_text_font(lbl_version, &lv_font_app_20, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_version, lv_color_hex(0x8A93A6), LV_PART_MAIN);
    lv_obj_align(lbl_version, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_scr_load(scr);
}
