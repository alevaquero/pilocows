#pragma once

// Create all LVGL objects for the session menu screen.
// Must be called once during ui_manager_init().
void screen_session_menu_create(void);

// Load (transition to) the session menu screen and refresh state from storage.
void screen_session_menu_load(void);

// Re-apply translated strings to every label — called when language changes.
void screen_session_menu_refresh_language(void);
