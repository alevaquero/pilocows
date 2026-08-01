#ifndef _UI_POPUP_H_
#define _UI_POPUP_H_

// Show a modal error popup (red) over the current active screen.
// The popup is self-contained: tapping "OK" deletes it.
void ui_popup_error(const char *msg);

// Show a modal info popup (neutral) over the current active screen.
void ui_popup_info(const char *msg);

#endif
