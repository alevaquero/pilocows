#pragma once

#include <stdint.h>
#include "../storage/session_storage.h"

// Create LVGL objects for the scan screen (called once during ui_manager_init).
void screen_scan_create(void);

// Load (make visible) the scan screen.
void screen_scan_load(void);

// Called from main task (under LVGL lock) when RFID fires.
// Flashes green (new) or red (duplicate).
// If duplicate: pre-fills data fields from the existing session record.
// If new: resets data fields (last weight is preserved for Weighing sessions).
void screen_scan_show_tag(const char *eid, bool is_duplicate);

// Fill *out with the current tag record from UI fields.
// Returns false if no EID is currently pending (nothing to save).
bool screen_scan_get_record(tag_record_t *out);

// Reset the screen to "Ready to scan" — clears EID and all data fields.
void screen_scan_clear_pending(void);

// Update the session info bar. Pass NULL to show "No active session" state.
// Called after session_create / session_set_active / session_clear_active.
void screen_scan_set_session(const session_meta_t *meta);

// Update the scanned animal count label.
void screen_scan_update_count(uint32_t count);

// Show a transient status message overlay (auto-clears after 2 s).
void screen_scan_show_status(const char *msg);

// Flash the full-screen overlay green (new) or red (duplicate).
// Must be called within display_lvgl_lock()/unlock().
void screen_scan_flash(bool duplicate);

// Refresh all translatable labels to the current language.
// Call after i18n_set_language(). Must be called within display_lvgl_lock()/unlock().
void screen_scan_refresh_language(void);
