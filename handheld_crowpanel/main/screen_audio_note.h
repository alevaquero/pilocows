#ifndef _SCREEN_AUDIO_NOTE_H_
#define _SCREEN_AUDIO_NOTE_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "ui_manager.h"

// Fired the moment a recording finishes (button release) — this screen
// auto-saves, it never stages an unconfirmed take. The callee should persist
// pcm/n_samples wherever it belongs (SD card via session_storage) before
// returning; the buffer is only valid for the duration of the call.
typedef void (*audio_note_recorded_cb_t)(const int16_t *pcm, size_t n_samples, void *user_data);

// Fired when the Delete icon is tapped — always means "remove whatever audio
// currently represents this note," whether that's a pre-existing SD file or
// one just recorded in this same screen visit.
typedef void (*audio_note_deleted_cb_t)(void *user_data);

typedef struct {
    const char *title;             // header title (already i18n'd by caller)
    bool has_existing;              // Play/Delete start enabled if true
    const char *existing_wav_path;  // SD path to the existing clip (ignored if has_existing=false)
    screen_id_t return_screen;      // where the back button navigates to
    audio_note_recorded_cb_t on_recorded;
    audio_note_deleted_cb_t on_deleted;
    void *user_data;
} audio_note_cfg_t;

void screen_audio_note_create(void);
void screen_audio_note_load(void);
void screen_audio_note_refresh_language(void);

// Configure and navigate to the recorder for one note. Calls
// ui_manager_show(SCREEN_AUDIO_NOTE) itself.
void screen_audio_note_show(const audio_note_cfg_t *cfg);

#endif
