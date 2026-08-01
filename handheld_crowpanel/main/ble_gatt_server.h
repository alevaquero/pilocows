#ifndef _BLE_GATT_SERVER_H_
#define _BLE_GATT_SERVER_H_

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// ── Sync status events (fired from the BLE task; marshal to LVGL via lv_async_call) ──
typedef enum {
    BLE_SYNC_IDLE,            // not advertising / no connection
    BLE_SYNC_CONNECTED,       // desktop just connected
    BLE_SYNC_SESSION_READING, // desktop is reading data for a session
    BLE_SYNC_SESSION_DONE,    // desktop marked a session as synced
    BLE_SYNC_DISCONNECTED,    // desktop disconnected
} ble_sync_status_t;

// detail is the session name (safe to copy; may be NULL).
typedef void (*ble_sync_status_cb_t)(ble_sync_status_t status, uint32_t session_id, const char *detail);

// Callback invoked when the desktop sends a CONTROL command.
typedef enum {
    BLE_CMD_SET_TIME, // payload: ISO 8601 UTC string
} ble_command_t;

typedef void (*ble_command_callback_t)(ble_command_t cmd, const char *payload);

// Initialize the BLE GATT server. Does NOT start advertising —
// call ble_gatt_server_start_advertising() when entering the Sync screen.
//
// NOTE: on CrowPanel (ESP32-P4), this requires the ESP32-C6 co-processor over
// ESP-HOSTED (the P4 has no native BLE radio, unlike the SC01's ESP32-S3).
// Until that bring-up is complete this is a stub: advertising/connection
// state never changes, matching a permanently-disconnected device.
esp_err_t ble_gatt_server_init(ble_command_callback_t on_command);

// Start BLE advertising so the handheld becomes discoverable.
void ble_gatt_server_start_advertising(void);

// Stop advertising and disconnect any active desktop connection.
void ble_gatt_server_stop_advertising(void);

// Register a callback for sync status events (NULL to deregister).
void ble_gatt_server_set_status_cb(ble_sync_status_cb_t cb);

// Update the DEVICE_STATUS characteristic (call after each scan).
void ble_gatt_server_update_status(uint32_t scan_count, const char *firmware_version);

// Returns true if a desktop is currently connected.
bool ble_gatt_server_is_connected(void);

// Terminate the active BLE connection (if any) and restart advertising.
void ble_gatt_server_disconnect(void);

esp_err_t ble_gatt_server_deinit(void);

#endif
