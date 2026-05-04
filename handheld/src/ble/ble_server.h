#pragma once

#include <stdbool.h>
#include <stdint.h>

// ── Sync status events (fired in BLE task; use lv_async_call in the UI cb) ───
typedef enum {
    BLE_SYNC_IDLE,            // not advertising / no connection
    BLE_SYNC_CONNECTED,       // desktop just connected
    BLE_SYNC_SESSION_READING, // desktop is reading data for a session
    BLE_SYNC_SESSION_DONE,    // desktop marked a session as synced
    BLE_SYNC_DISCONNECTED,    // desktop disconnected
} ble_sync_status_t;

// Callback invoked from the NimBLE task — must NOT touch LVGL directly.
// Use lv_async_call() to marshal updates to the LVGL task.
// detail is the session name (heap-safe to copy; may be NULL).
typedef void (*ble_sync_status_cb_t)(ble_sync_status_t status,
                                      uint32_t session_id,
                                      const char *detail);

// Callback invoked when the desktop sends a CONTROL command.
typedef enum {
    BLE_CMD_SET_TIME, // payload: ISO 8601 UTC string
} ble_command_t;

typedef void (*ble_command_callback_t)(ble_command_t cmd, const char *payload);

// Initialize NimBLE stack. Does NOT start advertising — call ble_server_start_advertising().
void ble_server_init(ble_command_callback_t on_command);

// Start BLE advertising so the handheld becomes discoverable.
// Call when entering the Sync screen.
void ble_server_start_advertising(void);

// Stop advertising and disconnect any active desktop connection.
// Call when leaving the Sync screen.
void ble_server_stop_advertising(void);

// Register a callback for sync status events (NULL to deregister).
void ble_server_set_status_cb(ble_sync_status_cb_t cb);

// Update the DEVICE_STATUS characteristic (call after each scan).
void ble_server_update_status(uint32_t scan_count, const char *firmware_version);

// Notify connected desktop that new scan data is available.
void ble_server_notify_scan_ready(void);

// Returns true if a desktop is currently connected.
bool ble_server_is_connected(void);

// Terminate the active BLE connection (if any) and restart advertising.
// Call when the sync UI is closed so the handheld goes back to discoverable.
void ble_server_disconnect(void);
