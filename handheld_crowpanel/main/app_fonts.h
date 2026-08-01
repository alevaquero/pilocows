#ifndef _APP_FONTS_H_
#define _APP_FONTS_H_

// App-wide fonts: Montserrat + Spanish accented characters + LVGL icon
// symbols, replacing LVGL's built-in ASCII-only lv_font_montserrat_XX so
// accented Spanish text (sesión, Año, configuración, ...) actually renders
// instead of showing blank glyphs.
#include "lvgl.h"

LV_FONT_DECLARE(lv_font_app_20);
LV_FONT_DECLARE(lv_font_app_24);
LV_FONT_DECLARE(lv_font_app_28);
LV_FONT_DECLARE(lv_font_app_30);
LV_FONT_DECLARE(lv_font_app_36);
LV_FONT_DECLARE(lv_font_app_44);

#endif
