#ifndef _SCREEN_SESSION_NEW_H_
#define _SCREEN_SESSION_NEW_H_

#include <stdint.h>

void screen_session_new_create(void);
void screen_session_new_load(void);
void screen_session_new_refresh_language(void);

// Opens this screen in edit mode for an existing session: name, text note,
// and voice note are all editable and pre-populated from the session's
// current data; type (and its vaccine/test sub-fields) is shown but locked
// — only session_create() ever sets it. Saving updates the existing
// session instead of creating a new one, and returns to the session list.
void screen_session_new_edit(uint32_t session_id);

#endif
