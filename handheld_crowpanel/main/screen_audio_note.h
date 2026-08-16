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
    // For a clip that's been recorded but not yet persisted to SD (e.g. a
    // staged session-note recording, still sitting in the caller's own PSRAM
    // buffer until the session itself is created) — set instead of
    // existing_wav_path so Play works when re-entering this screen for the
    // same note. Caller owns this memory; it must stay valid for as long as
    // the staged state exists (i.e. until the caller saves it to SD or the
    // user deletes it), since this screen only ever reads a pointer to it,
    // it does not copy the samples.
    const int16_t *existing_pcm;
    size_t existing_pcm_samples;
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
