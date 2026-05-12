#pragma once

#include <stdint.h>

typedef enum {
    SCREEN_SESSION_MENU,
    SCREEN_SESSION_NEW,
    SCREEN_SESSION_LIST,
    SCREEN_SCAN,
    SCREEN_HISTORY,
    SCREEN_SETTINGS,
    SCREEN_WIFI,
    SCREEN_VACCINE_SETTINGS,
    SCREEN_TEST_SETTINGS,
} screen_id_t;

// Initialize UI — call after display_init().
void ui_manager_init(void);

// Navigate to a screen.
void ui_manager_show(screen_id_t screen);

// Refresh the scan count on whichever screen shows it.
void ui_manager_update_scan_count(uint32_t count);

// Show a transient status message overlay (disappears after 2s).
void ui_manager_show_status(const char *msg);
