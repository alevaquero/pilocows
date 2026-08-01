#ifndef _UI_MANAGER_H_
#define _UI_MANAGER_H_

#include "session_storage.h"

typedef enum {
    SCREEN_SESSION_MENU,     // Home: active session card + navigation
    SCREEN_SESSION_NEW,      // Create new session dialog
    SCREEN_SESSION_LIST,     // List of past sessions
    SCREEN_SCAN,             // Main scan screen (type-specific panels)
    SCREEN_SETTINGS,         // Settings menu
    SCREEN_WIFI,             // WiFi settings (Phase 2)
    SCREEN_VACCINE_SETTINGS, // Vaccine config (Phase 2)
    SCREEN_TEST_SETTINGS,    // Test config (Phase 2)
    SCREEN_DATETIME,         // Date & time editor
    SCREEN_BLE_SYNC,         // BLE sync to PC
    SCREEN_MIC_TEST,         // Microphone recording test
    SCREEN_AUDIO_NOTE,       // Reusable audio-note recorder (session/tag voice notes)
    SCREEN_COUNT
} screen_id_t;

// Initialize UI manager (call once after display_init)
void ui_manager_init(void);

// Navigate to a screen
void ui_manager_show(screen_id_t screen);

// Update scan count display on relevant screens
void ui_manager_update_scan_count(uint32_t count);

// Show transient status message (auto-clears after 2s)
void ui_manager_show_status(const char *msg);

// Refresh all translatable labels (call after language change)
void ui_manager_refresh_language(void);

#endif
