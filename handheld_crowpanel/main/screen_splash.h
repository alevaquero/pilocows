#ifndef SCREEN_SPLASH_H
#define SCREEN_SPLASH_H

// Full-screen boot splash. Created and loaded directly — unlike every other
// screen in this app, it's shown before ui_manager (and the session
// storage / settings it depends on) is initialized, so it can't go through
// ui_manager's screen_id_t/create-all-screens pattern. main.c calls this
// right after display_init() succeeds, then keeps it on screen for the
// rest of app_main()'s init work, switching away via ui_manager_show()
// only once boot is fully complete.
void screen_splash_show(void);

#endif
