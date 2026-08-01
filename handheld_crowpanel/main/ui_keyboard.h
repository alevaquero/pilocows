#ifndef UI_KEYBOARD_H
#define UI_KEYBOARD_H

#include "lvgl.h"

// Creates a keyboard styled the same way everywhere it's used in this app:
// compact height (was stretched tall), smaller font, rounded keys, and an
// iOS-inspired layout (dedicated backspace/shift/123-toggle/space/return
// keys, wide space bar) instead of LVGL's stock QWERTY map. Bottom-aligned
// to its parent by default; caller still owns show/hide, textarea binding,
// and READY/CANCEL event callbacks.
lv_obj_t *ui_keyboard_create(lv_obj_t *parent);

#endif
