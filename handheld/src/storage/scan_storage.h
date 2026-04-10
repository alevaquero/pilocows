#pragma once

#include "rfid/rfid_reader.h"
#include <stdint.h>
#include <stdbool.h>

#define SCAN_EVENT_GENERAL       "general"
#define SCAN_EVENT_WEIGHING      "weighing"
#define SCAN_EVENT_VACCINATION   "vaccination"
#define SCAN_EVENT_PREGNANCY     "pregnancy_check"
#define SCAN_EVENT_TB_TEST       "tb_test"
#define SCAN_EVENT_REMOVAL       "removal"

typedef struct {
    char     eid[16];           // FDX-B EID (15 digits + null)
    char     scanned_at[25];    // ISO 8601 UTC: "2026-04-09T10:32:00Z"
    char     event_type[24];    // One of SCAN_EVENT_* above
    char     notes[64];         // Optional free-text notes
} scan_record_t;

// Initialize storage (mounts SPIFFS).
void scan_storage_init(void);

// Save a new scan record. Returns true on success.
bool scan_storage_save(const scan_record_t *record);

// Get total number of stored scans.
uint32_t scan_storage_count(void);

// Get all records as a JSON string into buf (caller provides buffer).
// Returns number of bytes written, or -1 if buffer too small.
int scan_storage_to_json(char *buf, size_t buf_len);

// Delete all stored scans. Called after successful BLE sync.
void scan_storage_clear(void);
