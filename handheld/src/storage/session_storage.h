#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

// ── Limits ────────────────────────────────────────────────────────────────────
#define SESSION_NAME_MAX    64
#define SESSION_EID_MAX     16
#define SESSION_DATA_SIZE   16
#define SESSION_NOTE_MAX   128
#define SESSION_VAX_MAX     15   // vaccines per session
#define VACCINE_NAME_MAX    32
#define VACCINE_LIST_MAX    20
#define TEST_NAME_MAX       32
#define TEST_LIST_MAX       20

// ── Session type (replaces the old "event type" concept) ──────────────────────
typedef enum {
    SESSION_TYPE_GENERAL     = 0,
    SESSION_TYPE_WEIGHING    = 1,
    SESSION_TYPE_VACCINATION = 2,
    SESSION_TYPE_PREGNANCY   = 3,
    SESSION_TYPE_TEST        = 4,   // Generic configurable test (Positive/Negative/Inconclusive)
    SESSION_TYPE_REMOVAL     = 5,
    SESSION_TYPE_COUNT
} session_type_t;

// ── Session deletion marker (internal — not exposed as open/closed) ──────────
// _deleted == 0: normal session. _deleted != 0: tombstoned (treat as gone).

// ── Per-tag data payload types ────────────────────────────────────────────────
typedef enum {
    PREGNANCY_UNKNOWN  = 0,
    PREGNANCY_NO       = 1,
    PREGNANCY_SMALL    = 2,
    PREGNANCY_MEDIUM   = 3,
    PREGNANCY_BIG      = 4,
    PREGNANCY_REJECTED = 5,
} pregnancy_result_t;

typedef enum {
    TEST_INCONCLUSIVE = 0,
    TEST_POSITIVE     = 1,
    TEST_NEGATIVE     = 2,
} test_result_t;

// ── Session metadata — 256 bytes, stored in the sessions index file ───────────
// Offset in index file = (id - 1) * sizeof(session_meta_t).
// id == 0 is a tombstone (deleted record).
typedef struct __attribute__((packed)) {
    uint32_t id;                        //   4 — 1-based; 0 = tombstone
    char     name[SESSION_NAME_MAX];    //  64 — display name, user-editable
    uint8_t  type;                      //   1 — session_type_t
    uint8_t  _deleted;                  //   1 — 0=normal, 1=tombstoned (internal)
    time_t   created_at;                //   4 — Unix timestamp (UTC)
    uint32_t tag_count;                 //   4 — unique animals in this session
    uint8_t  vax_count;                 //   1 — vaccines selected (type VACCINATION only)
    uint8_t  vax_ids[SESSION_VAX_MAX];  //  15 — vaccine IDs from vaccine config
    uint8_t  synced;                    //   1 — 1 = marked synced by desktop
    char     note[SESSION_NOTE_MAX];    // 128 — free-text session note
    uint8_t  test_id;                   //   1 — test config ID (type TEST only; 0=none)
    uint8_t  _pad[32];                  //  32 — reserved for future use
} session_meta_t;                       // TOTAL: 256 bytes

// ── Tag scan record — 164 bytes, stored in per-session record files ───────────
// One record per unique EID per session. Overwritten on re-scan (not appended).
//
// data[] payload layout by session type:
//   General:    data[0..15]  unused
//   Weighing:   data[0..1]   uint16_t weight_kg (little-endian)
//   Vaccination:data[0]      vax_count, data[1..15] vax_ids (copy from session)
//   Pregnancy:  data[0]      pregnancy_result_t
//   Test:       data[0]      test_result_t
//   Removal:    data[0..3]   time_t removal_date (= scanned_at at first scan)
typedef struct __attribute__((packed)) {
    char     eid[SESSION_EID_MAX];      // 16 — FDX-B EID (primary key)
    time_t   scanned_at;                //  4 — UTC timestamp of scan
    uint8_t  data[SESSION_DATA_SIZE];   // 16 — type-specific payload
    char     note[SESSION_NOTE_MAX];    // 128 — free-text note (may be empty)
} tag_record_t;                         // TOTAL: 164 bytes

// ── Vaccine configuration record — 36 bytes ───────────────────────────────────
// Stored in a flat file; soft-deleted (active=0) to keep IDs stable.
typedef struct __attribute__((packed)) {
    uint8_t  id;                        //  1 — 1-based; 0 = tombstone
    char     name[VACCINE_NAME_MAX];    // 32 — display name
    uint8_t  active;                    //  1 — 1 = in use, 0 = deleted
    uint8_t  _pad[2];                   //  2
} vaccine_cfg_t;                        // TOTAL: 36 bytes

// ─────────────────────────────────────────────────────────────────────────────
// Storage API
// ─────────────────────────────────────────────────────────────────────────────

// Initialize storage and mount SPIFFS. Safe to call even if SPIFFS was already
// mounted — detects ESP_ERR_INVALID_STATE and reuses the existing mount.
// Also restores the last active session from NVS.
esp_err_t session_storage_init(void);

// ── Session management ────────────────────────────────────────────────────────

// Create a new session, auto-naming it "YYYY-MM-DD <type>" if name is NULL.
// vax_ids / vax_count are only used for SESSION_TYPE_VACCINATION.
// test_id is only used for SESSION_TYPE_TEST (0 = no test selected).
// Sets the new session as the active session.
// Returns the new session ID in *out_id (may be NULL).
esp_err_t session_create(session_type_t type, const char *name,
                          const uint8_t *vax_ids, uint8_t vax_count,
                          uint8_t test_id,
                          uint32_t *out_id);

// Set an existing session as the active (current) session.
esp_err_t session_set_active(uint32_t id);

// Clear the active session pointer without deleting it.
void      session_clear_active(void);

// Copy active session metadata into *out. Returns false if no active session.
bool      session_get_active(session_meta_t *out);

// List all non-deleted sessions, newest first. Returns the count written.
// max_count is capped at an internal limit of 50.
int       session_list_all(session_meta_t *out, int max_count);

// Permanently delete a session and its record file.
// The active session cannot be deleted; close it first.
esp_err_t session_delete(uint32_t id);

// Read metadata for any session by id (not just the active one).
esp_err_t session_get_meta(uint32_t id, session_meta_t *out);

// Mark a session as synced (sets session_meta_t.synced = 1).
esp_err_t session_mark_synced(uint32_t id);

// Update the free-text note for any session (not just the active one).
esp_err_t session_save_note(uint32_t id, const char *note);

// Read all tag records for any session (not just the active one).
// Records are returned in scan order (first scanned first).
// Returns the number of records written (up to max_count).
int session_list_records(uint32_t session_id, tag_record_t *out, int max_count);

// Paged variant: skip 'offset' records then read up to max_count.
// Used by BLE sync to stay within per-read BLE payload limits.
int session_list_records_paged(uint32_t session_id, tag_record_t *out,
                               int max_count, uint32_t offset);

// ── Tag record API (operates on the active session) ───────────────────────────

// Save a tag record for the active session.
//   • If a record with the same EID exists → overwrite (tag_count unchanged).
//   • If the EID is new → append, increment tag_count.
// Returns ESP_ERR_INVALID_STATE if no active session.
esp_err_t session_save_record(const tag_record_t *rec);

// Look up a tag record by EID in the active session.
// Returns false if not found or no active session.
bool      session_find_record(const char *eid, tag_record_t *out);

// Number of unique animals scanned in the active session (0 if none active).
uint32_t  session_get_tag_count(void);

// ── Vaccine configuration API ─────────────────────────────────────────────────

// Load all active (non-deleted) vaccines. Returns count written.
int       vaccine_list(vaccine_cfg_t *out, int max_count);

// Add a new vaccine. Assigns the next available ID and writes *out_id.
esp_err_t vaccine_add(const char *name, uint8_t *out_id);

// Soft-delete a vaccine by ID.
esp_err_t vaccine_delete(uint8_t id);

// Resolve a vaccine ID to its name. Returns false if not found or inactive.
bool      vaccine_get_name(uint8_t id, char *out_name, size_t max_len);

// ── Test configuration (generic configurable test) ────────────────────────────

// Stored same way as vaccines but in a separate file.
typedef struct __attribute__((packed)) {
    uint8_t  id;                        //  1 — 1-based; 0 = tombstone
    char     name[TEST_NAME_MAX];       // 32 — display name
    uint8_t  active;                    //  1 — 1 = in use, 0 = deleted
    uint8_t  _pad[2];                   //  2
} test_cfg_t;                           // TOTAL: 36 bytes

// Load all active (non-deleted) tests. Returns count written.
int       test_list(test_cfg_t *out, int max_count);

// Add a new test. Assigns the next available ID and writes *out_id.
esp_err_t test_add(const char *name, uint8_t *out_id);

// Soft-delete a test by ID.
esp_err_t test_delete(uint8_t id);

// Resolve a test ID to its name. Returns false if not found or inactive.
bool      test_get_name(uint8_t id, char *out_name, size_t max_len);

// ── Utility ───────────────────────────────────────────────────────────────────

// Build the automatic session name: "2026-04-17 Weighing".
void session_build_default_name(session_type_t type, char *buf, size_t len);
