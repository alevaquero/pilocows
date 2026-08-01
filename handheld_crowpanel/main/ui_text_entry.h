#ifndef UI_TEXT_ENTRY_H
#define UI_TEXT_ENTRY_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*ui_text_entry_confirm_cb_t)(const char *text, void *user_data);
typedef void (*ui_text_entry_cancel_cb_t)(void *user_data);

typedef struct {
    const char *label;        // Field label shown above the input (already i18n'd by caller)
    const char *initial_text; // Prefilled text; the underlying field is never touched unless OK is pressed
    const char *placeholder;  // Optional placeholder (NULL for none, already i18n'd by caller)
    bool multiline;           // false = one line (name/password), true = multi-line note
    bool password;            // true = masked with an eye-toggle button
    uint16_t max_length;      // 0 = no explicit limit
    ui_text_entry_confirm_cb_t on_confirm; // called with the final text if OK is tapped
    ui_text_entry_cancel_cb_t on_cancel;   // called if Cancel is tapped (may be NULL)
    void *user_data;
} ui_text_entry_cfg_t;

// Shows the fullscreen text-entry modal used consistently everywhere in this
// app: label on top, input field below it, a Cancel (red) / OK (default
// accent) row right above the on-screen keyboard. Editing happens in the
// modal's own temporary field - the caller's real widget/data is only
// touched from on_confirm, so Cancel is always a true no-op revert.
void ui_text_entry_show(const ui_text_entry_cfg_t *cfg);

#endif
