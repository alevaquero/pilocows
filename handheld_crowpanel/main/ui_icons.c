#include "ui_icons.h"

void ui_icon_create(lv_obj_t *parent, const char *symbol, lv_color_t color, const lv_font_t *font) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, symbol);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
    lv_obj_center(lbl);
}
