#pragma once

#include <stdbool.h>
#include <stdint.h>

// Callback invoked when the desktop sends a CONTROL command.
typedef enum {
    BLE_CMD_CLEAR_LIST,
    BLE_CMD_SET_TIME,   // payload: ISO 8601 UTC string
} ble_command_t;

typedef void (*ble_command_callback_t)(ble_command_t cmd, const char *payload);

// Initialize NimBLE stack and start advertising.
// The device is named "Pilocows" and exposes the Pilocows Scan GATT service.
void ble_server_init(ble_command_callback_t on_command);

// Update the DEVICE_STATUS characteristic (call after each scan).
void ble_server_update_status(uint32_t scan_count, const char *firmware_version);

// Notify connected desktop that new scan data is available.
void ble_server_notify_scan_ready(void);

// Returns true if a desktop is currently connected.
bool ble_server_is_connected(void);
