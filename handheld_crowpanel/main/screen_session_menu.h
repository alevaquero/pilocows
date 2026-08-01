#ifndef _SCREEN_SESSION_MENU_H_
#define _SCREEN_SESSION_MENU_H_

// Create the session menu screen (called once during ui_manager_init)
void screen_session_menu_create(void);

// Load/show the session menu screen (also refreshes dynamic content)
void screen_session_menu_load(void);

// Refresh translatable labels
void screen_session_menu_refresh_language(void);

#endif
