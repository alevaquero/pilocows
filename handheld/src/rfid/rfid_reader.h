#pragma once

#include <stdint.h>
#include <stdbool.h>

#define RFID_EID_LEN 15  // FDX-B EID: 15 decimal digits (64-bit identifier)

typedef struct {
    char eid[RFID_EID_LEN + 1];  // Null-terminated EID string
} rfid_tag_t;

// Callback invoked from the RFID reader task when a new tag is detected.
// Called from the RFID task context — keep it short, post to a queue.
typedef void (*rfid_tag_callback_t)(const rfid_tag_t *tag);

// Initialize UART and start the RFID reader task.
void rfid_init(rfid_tag_callback_t on_tag_read);

// Stop the RFID reader task and release UART.
void rfid_deinit(void);

// Enable or disable continuous scanning.
void rfid_set_scanning(bool enabled);
