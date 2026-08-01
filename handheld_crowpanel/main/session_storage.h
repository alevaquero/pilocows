#ifndef _SESSION_STORAGE_H_
#define _SESSION_STORAGE_H_

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// Session types matching original handheld
typedef enum {
    SESSION_TYPE_GENERAL = 0,
    SESSION_TYPE_WEIGHING,
    SESSION_TYPE_VACCINATION,
    SESSION_TYPE_PREGNANCY,
    SESSION_TYPE_TEST,
    SESSION_TYPE_REMOVAL,
} session_type_t;

// Test result options
typedef enum {
    TEST_INCONCLUSIVE = 0,
    TEST_NEGATIVE,
    TEST_POSITIVE,
} test_result_t;

// Pregnancy result options
typedef enum {
    PREGNANCY_UNKNOWN = 0,
    PREGNANCY_NO,
    PREGNANCY_REJECTED,
    PREGNANCY_SMALL,
    PREGNANCY_MEDIUM,
    PREGNANCY_BIG,
} pregnancy_result_t;

// Session metadata
typedef struct {
    uint32_t id;                    // Unique session ID (sequential, 1-based; 0 = invalid/tombstone)
    char name[64];                  // Session name (e.g., "Weighing 2026-07-25")
    session_type_t type;            // Session type
    uint32_t tag_count;             // Number of scanned tags
    time_t created_at;              // Creation timestamp
    time_t updated_at;              // Last update timestamp
    char note[128];                 // Free-text session note
    uint8_t vax_ids[15];            // Vaccine IDs selected (type VACCINATION only)
    uint8_t vax_count;              // Number of vaccine IDs used
    uint8_t test_id;                // Test config ID (type TEST only; 0 = none)
    uint8_t synced;                 // 1 = marked synced by desktop (BLE)
    uint8_t deleted;                // 0 = normal, 1 = tombstoned
    uint8_t has_note_audio;         // 1 = a voice note exists on the SD card
} session_meta_t;

// Tag record within a session
typedef struct {
    char eid[16];                   // 11-digit EID + null terminator
    time_t scanned_at;              // Timestamp of scan

    // Type-specific data
    uint16_t weight_kg;             // Weighing
    pregnancy_result_t pregnancy;   // Pregnancy
    test_result_t test_result;      // Test result
    char vaccines[256];             // Comma-separated vaccine names
    char removal_reason[128];       // Removal reason
    char notes[256];                // Per-animal notes
    uint8_t has_audio;              // 1 = a voice note exists on the SD card
    uint16_t audio_seq;             // File index within the session's audio dir (t<seq>.wav)
} tag_record_t;

// Vaccine / test configuration entries (shared soft-delete design)
typedef struct {
    uint8_t id;                     // 1-based; 0 = tombstone
    char name[32];
    uint8_t active;                 // 1 = in use, 0 = deleted
} vaccine_cfg_t;

typedef struct {
    uint8_t id;                     // 1-based; 0 = tombstone
    char name[32];
    uint8_t active;                 // 1 = in use, 0 = deleted
} test_cfg_t;

// Session limits
#define MAX_SESSIONS            100
#define MAX_TAGS_PER_SESSION    500
#define SESSION_NAME_MAX        64
#define SESSION_NOTE_MAX        128
#define SESSION_EID_MAX         15
#define SESSION_VAX_MAX         15
#define VACCINE_NAME_MAX        32
#define VACCINE_LIST_MAX        20
#define TEST_NAME_MAX           32
#define TEST_LIST_MAX           20

// ===== Session CRUD Operations =====

// Create a new session (returns session ID, 0 on error). If name is NULL/empty,
// auto-names it "YYYY-MM-DD <type>". vax_ids/vax_count only apply to
// SESSION_TYPE_VACCINATION; test_id only applies to SESSION_TYPE_TEST (0 = none).
// Sets the new session as active.
uint32_t session_create(const char *name, session_type_t type,
                         const uint8_t *vax_ids, uint8_t vax_count, uint8_t test_id);

// Get active session metadata (returns true if active session exists)
bool session_get_active(session_meta_t *out);

// Set active session (or pass 0 to clear)
esp_err_t session_set_active(uint32_t session_id);

// Get session by ID
esp_err_t session_get(uint32_t id, session_meta_t *out);

// List all non-deleted sessions, newest first (returns count, max_out sessions in array)
uint32_t session_list(session_meta_t *out, uint32_t max_out);

// Close/finalize a session (clears active pointer if it matches)
esp_err_t session_close(uint32_t session_id);

// Delete a session (tombstones metadata, removes its tag record file)
esp_err_t session_delete(uint32_t session_id);

// Update the free-text note for a session
esp_err_t session_save_note(uint32_t session_id, const char *note);

// Mark a session as synced (sets session_meta_t.synced = 1). Used by BLE sync.
esp_err_t session_mark_synced(uint32_t session_id);

// ===== Audio notes (SD card) =====
// PCM is always 16kHz/16-bit mono — written to disk as a standard 44-byte-
// header WAV. All of these are no-ops (return ESP_ERR_INVALID_STATE) if the
// SD card isn't mounted; callers should treat that as "audio unavailable",
// not a hard failure.

// SD paths for a session's/tag's audio, written into buf regardless of
// whether the file actually exists yet — callers check has_note_audio /
// has_audio first.
void session_note_audio_path(uint32_t session_id, char *buf, size_t len);
void session_tag_audio_path(uint32_t session_id, uint16_t audio_seq, char *buf, size_t len);

// Save/overwrite a session's note audio; sets has_note_audio=1.
esp_err_t session_save_note_audio(uint32_t session_id, const int16_t *pcm, size_t n_samples);

// Delete a session's note audio; clears has_note_audio.
esp_err_t session_delete_note_audio(uint32_t session_id);

// Save the ACTIVE session's audio for a tag at the given seq (typically the
// tag's tag_count right before session_add_tag() is called for it) — mirrors
// session_add_tag()'s "operates on the active session" convention.
esp_err_t session_save_tag_audio(uint16_t audio_seq, const int16_t *pcm, size_t n_samples);

// Read tag records for ANY session (not just the active one), in scan order,
// skipping 'offset' records then reading up to max_out. Used by BLE sync to
// stay within per-read payload limits (MTU) when reading historical sessions.
uint32_t session_list_records_paged(uint32_t session_id, tag_record_t *out,
                                     uint32_t max_out, uint32_t offset);

// ===== Tag Record Operations (operate on the active session) =====

// Add or update a tag in the active session (upsert by EID; increments
// tag_count only when the EID is new).
esp_err_t session_add_tag(const tag_record_t *tag);

// Get tag from active session by EID (returns true if found)
bool session_get_tag(const char *eid, tag_record_t *out);

// Update an existing tag in the active session (fails if EID not found)
esp_err_t session_update_tag(const tag_record_t *tag);

// List tags in active session, in scan order (returns count)
uint32_t session_list_tags(tag_record_t *out, uint32_t max_out);

// Get tag count for active session
uint32_t session_get_tag_count(void);

// Clear all tags from active session
esp_err_t session_clear_tags(void);

// ===== Vaccine configuration =====

uint32_t vaccine_list(vaccine_cfg_t *out, uint32_t max_out);
esp_err_t vaccine_add(const char *name, uint8_t *out_id);
esp_err_t vaccine_delete(uint8_t id);
bool vaccine_get_name(uint8_t id, char *out_name, size_t max_len);

// ===== Test configuration =====

uint32_t test_list(test_cfg_t *out, uint32_t max_out);
esp_err_t test_add(const char *name, uint8_t *out_id);
esp_err_t test_delete(uint8_t id);
bool test_get_name(uint8_t id, char *out_name, size_t max_len);

// Build the automatic session name: "2026-04-17 Weighing".
void session_build_default_name(session_type_t type, char *buf, size_t len);

// ===== Initialization =====

// Initialize session storage (mounts SPIFFS, restores active session; call once on startup)
esp_err_t session_storage_init(void);

#endif
