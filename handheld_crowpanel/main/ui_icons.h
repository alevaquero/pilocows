#ifndef _UI_ICONS_H_
#define _UI_ICONS_H_

#include "lvgl.h"

// Material Design Icons glyphs, embedded in this project's custom app fonts
// (main/lv_font_app_*.c) alongside Montserrat and the LVGL FontAwesome
// symbol subset. Used for every icon button app-wide instead of mixing
// FontAwesome (LV_SYMBOL_*) and MDI glyphs, which look inconsistently
// sized/weighted even at the same nominal font size — different icon
// foundries pad their glyphs differently within the em box.
//
// To regenerate the app fonts (e.g. to add another MDI glyph), the
// materialdesignicons-webfont.ttf source is not vendored in this repo —
// fetch it via `npm install @mdi/font` and use
// node_modules/@mdi/font/fonts/materialdesignicons-webfont.ttf. Look up the
// glyph's codepoint in node_modules/@mdi/font/css/materialdesignicons.css
// (e.g. ".mdi-pencil::before { content: "\F03EB"; }" - convert hex to
// decimal for lv_font_conv's `-r` range), and merge it into the same
// --font list already used for lv_font_app_*.c (see the Opts comment at the
// top of any of those files for the full existing command).
#define UI_SYMBOL_MIC          "\xF3\xB0\x8D\xAC" // U+F036C mdi-microphone
#define UI_SYMBOL_BACK         "\xF3\xB0\x81\x8D" // U+F004D mdi-arrow-left
#define UI_SYMBOL_EDIT         "\xF3\xB0\x8F\xAB" // U+F03EB mdi-pencil
#define UI_SYMBOL_PLAY         "\xF3\xB0\x90\x8A" // U+F040A mdi-play
#define UI_SYMBOL_TRASH        "\xF3\xB0\xA9\xB9" // U+F0A79 mdi-trash-can
#define UI_SYMBOL_REPEAT       "\xF3\xB0\x91\x96" // U+F0456 mdi-repeat
#define UI_SYMBOL_PLUS         "\xF3\xB0\x90\x95" // U+F0415 mdi-plus
#define UI_SYMBOL_SETTINGS     "\xF3\xB0\x92\x93" // U+F0493 mdi-cog
#define UI_SYMBOL_SAVE         "\xF3\xB0\x86\x93" // U+F0193 mdi-content-save
#define UI_SYMBOL_REFRESH      "\xF3\xB0\x91\x90" // U+F0450 mdi-refresh
#define UI_SYMBOL_UP           "\xF3\xB0\x85\x83" // U+F0143 mdi-chevron-up
#define UI_SYMBOL_DOWN         "\xF3\xB0\x85\x80" // U+F0140 mdi-chevron-down
#define UI_SYMBOL_CHECK        "\xF3\xB0\x84\xAC" // U+F012C mdi-check

// Creates a centered icon label inside `parent` (usually a button), using
// one of the UI_SYMBOL_* glyphs above at the given font/color. Pick a font
// size proportional to parent's size (e.g. lv_font_app_30 for a ~70px
// button) — matching the font size other icon buttons of the same physical
// size already use is what actually makes icons look "uniform," since the
// glyphs themselves now all share one foundry's proportions.
void ui_icon_create(lv_obj_t *parent, const char *symbol, lv_color_t color, const lv_font_t *font);

#endif
