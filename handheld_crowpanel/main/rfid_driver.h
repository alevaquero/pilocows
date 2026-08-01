#ifndef _RFID_DRIVER_H_
#define _RFID_DRIVER_H_

#include "esp_err.h"
#include <stdint.h>

#define RFID_EID_LEN 15  // FDX-B EID: 3-digit country code + 12-digit animal ID

typedef struct {
    char eid[RFID_EID_LEN + 1];  // Null-terminated EID string
    uint32_t timestamp;
} RfidScan;

typedef void (*rfid_callback_t)(const RfidScan *scan);

// Initialize RFID UART driver (UART1, RX on GPIO 26) and start the reader task.
esp_err_t rfid_init(rfid_callback_t callback);

// Deinitialize RFID driver
esp_err_t rfid_deinit(void);

// Get current scan count (for UI updates)
uint32_t rfid_get_scan_count(void);

// Clear scan counter
void rfid_clear_scans(void);

#endif
