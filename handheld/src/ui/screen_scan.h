#pragma once

#include <stdint.h>

// Create LVGL objects for the scan screen (called once during ui_manager_init).
void screen_scan_create(void);

// Load (make visible) the scan screen.
void screen_scan_load(void);

// Update the displayed EID and status after a successful scan.
void screen_scan_show_tag(const char *eid, const char *event_type);

// Update the scan counter label.
void screen_scan_update_count(uint32_t count);

// Show a brief status overlay message.
void screen_scan_show_status(const char *msg);

// Update the event type selector (called from settings or event picker).
void screen_scan_set_event_type(const char *event_type);
