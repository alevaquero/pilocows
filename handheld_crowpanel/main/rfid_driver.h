#ifndef _RFID_DRIVER_H_
#define _RFID_DRIVER_H_

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    char eid[16];           // 11-digit EID + null terminator
    uint32_t timestamp;
} RfidScan;

typedef void (*rfid_callback_t)(const RfidScan *scan);

// Initialize RFID UART driver (GPIO 10 RX, GPIO 11 TX)
esp_err_t rfid_init(rfid_callback_t callback);

// Deinitialize RFID driver
esp_err_t rfid_deinit(void);

// Get current scan count (for UI updates)
uint32_t rfid_get_scan_count(void);

// Clear all scans
void rfid_clear_scans(void);

#endif
