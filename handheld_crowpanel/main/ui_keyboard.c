#include "ui_keyboard.h"
#include "app_fonts.h"
#include <string.h>

// Compact size (was 480x400 - very stretched per-key height across the app).
#define KB_WIDTH  480
#define KB_HEIGHT 260

// LV_BTNMATRIX_CTRL_POPOVER shows an enlarged bubble of the pressed key,
// matching the iOS key-press feedback (enabled via lv_keyboard_set_popovers).
#define KB_POP(w) (LV_BTNMATRIX_CTRL_POPOVER | (w))

// IMPORTANT: per-button width occupies only the low 4 bits of the ctrl value
// (LV_BTNMATRIX_WIDTH_MASK = 0x000F), so widths must stay 1-15. A width of 16
// silently overflows into bit 0x10, which is LV_BTNMATRIX_CTRL_HIDDEN - that
// exact bug previously made the space bar reserve its layout slot but never
// actually render or accept touches.

// ── Lowercase ────────────────────────────────────────────────────────────────
// Row1: q..p (10, no backspace - moved to row3 like iOS)
// Row2: a..l (9)
// Row3: shift (up-arrow icon) | z..m | backspace (9)
// Row4: 123 | space (dominant) | . | return (4)
static const char *const s_kb_map_lc[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    LV_SYMBOL_UP, "z", "x", "c", "v", "b", "n", "m", LV_SYMBOL_BACKSPACE, "\n",
    "123", " ", ".", LV_SYMBOL_NEW_LINE, "",
};

static const lv_btnmatrix_ctrl_t s_kb_ctrl_lc[] = {
    KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4),
    KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4),
    LV_KEYBOARD_CTRL_BTN_FLAGS | 6, KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 14, KB_POP(2), LV_BTNMATRIX_CTRL_CHECKED | 3,
};

// ── Uppercase (same shape as lowercase) ─────────────────────────────────────
// Shift key is CHECKED here to show it's "active" while caps is on.
static const char *const s_kb_map_uc[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    LV_SYMBOL_UP, "Z", "X", "C", "V", "B", "N", "M", LV_SYMBOL_BACKSPACE, "\n",
    "123", " ", ".", LV_SYMBOL_NEW_LINE, "",
};

static const lv_btnmatrix_ctrl_t s_kb_ctrl_uc[] = {
    KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4),
    KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4),
    LV_KEYBOARD_CTRL_BTN_FLAGS | LV_BTNMATRIX_CTRL_CHECKED | 6, KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 14, KB_POP(2), LV_BTNMATRIX_CTRL_CHECKED | 3,
};

// ── Numbers/symbols (iOS "123" page - skips the deeper "#+=" page) ─────────
// Row1: 1..0 (10)  Row2: common symbols (10)
// Row3: punctuation + backspace (6)  Row4: ABC | space (dominant) | return (3)
static const char *const s_kb_map_spec[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "-", "/", ":", ";", "(", ")", "$", "&", "@", "\"", "\n",
    ".", ",", "?", "!", "'", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", " ", LV_SYMBOL_NEW_LINE, "",
};

static const lv_btnmatrix_ctrl_t s_kb_ctrl_spec[] = {
    KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4),
    KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4),
    KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), KB_POP(4), LV_BTNMATRIX_CTRL_CHECKED | 6,
    LV_KEYBOARD_CTRL_BTN_FLAGS | 3, 14, LV_BTNMATRIX_CTRL_CHECKED | 3,
};

static bool s_maps_installed = false;

// iOS-style one-shot shift: tapping the arrow capitalizes only the next key
// press, then automatically drops back to lowercase (unlike a caps-lock
// toggle that stays on until pressed again).
static bool s_shift_once_pending = false;

// LVGL's built-in lv_keyboard_def_event_cb only recognizes a fixed set of
// literal strings for mode switching ("1#", "abc", "ABC" - not "123", and not
// icon glyphs like LV_SYMBOL_UP at all), so our custom labels need their own
// handling here; everything else (letters, backspace, space, period, return)
// still delegates to the default handler unchanged.
static void ui_keyboard_event_cb(lv_event_t *e) {
    lv_obj_t *kb = lv_event_get_target(e);
    uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
    if (btn_id != LV_BTNMATRIX_BTN_NONE) {
        const char *txt = lv_btnmatrix_get_btn_text(kb, btn_id);
        if (txt) {
            if (strcmp(txt, LV_SYMBOL_UP) == 0) {
                if (lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_TEXT_UPPER) {
                    // Pressed again before typing anything - cancel back to lowercase.
                    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
                    s_shift_once_pending = false;
                } else {
                    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
                    s_shift_once_pending = true;
                }
                return;
            }
            if (strcmp(txt, "123") == 0) {
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
                s_shift_once_pending = false;
                return;
            }
            if (strcmp(txt, "ABC") == 0) {
                lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
                s_shift_once_pending = false;
                return;
            }
        }
    }

    // Let the default handler process the key (inserting the uppercase
    // letter, etc.) before dropping back to lowercase, so this key still
    // gets typed in the case the shift press promised.
    lv_keyboard_def_event_cb(e);
    if (s_shift_once_pending) {
        s_shift_once_pending = false;
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    }
}

lv_obj_t *ui_keyboard_create(lv_obj_t *parent) {
    lv_obj_t *kb = lv_keyboard_create(parent);

    // lv_keyboard_set_map() writes into LVGL's module-level map table shared
    // by every keyboard instance in the app, so installing it once here makes
    // every ui_keyboard_create() call (across all screens) consistent for free.
    if (!s_maps_installed) {
        lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, s_kb_map_lc, s_kb_ctrl_lc);
        lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, s_kb_map_uc, s_kb_ctrl_uc);
        lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_SPECIAL, s_kb_map_spec, s_kb_ctrl_spec);
        s_maps_installed = true;
    }

    lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(kb, ui_keyboard_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_set_size(kb, KB_WIDTH, KB_HEIGHT);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(kb, &lv_font_app_24, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_pad_row(kb, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 4, LV_PART_MAIN);
    lv_keyboard_set_popovers(kb, true);

    return kb;
}
