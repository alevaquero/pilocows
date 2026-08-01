#ifndef _WIFI_MANAGER_H_
#define _WIFI_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

// One AP entry returned by a scan — sorted by RSSI (strongest first).
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open; // true = no password required
} wifi_ap_t;

typedef void (*wifi_on_connected_cb_t)(void);

// Callback invoked after a scan completes. |aps| is only valid during the call.
typedef void (*wifi_on_scan_done_cb_t)(const wifi_ap_t *aps, uint16_t count);

// Callback invoked when a connection attempt fails due to wrong credentials.
typedef void (*wifi_on_error_cb_t)(void);

// NOTE: on CrowPanel (ESP32-P4), WiFi requires the ESP32-C6 co-processor over
// ESP-HOSTED (the P4 has no native WiFi radio, unlike the SC01's ESP32-S3).
// Until that bring-up is complete this is a stub: scans always return zero
// networks and connection attempts always fail, matching a radio-less device.

// Must be called after nvs_flash_init(). Reads stored credentials and
// connects in the background if present.
void wifi_manager_init(void);

// Trigger an async WiFi scan. Ignored if a scan is already in progress.
void wifi_scan_start(wifi_on_scan_done_cb_t cb);

// Persist SSID + password to NVS and immediately attempt connection.
void wifi_set_credentials(const char *ssid, const char *pass);

bool wifi_is_connected(void);
const char *wifi_get_ip_str(void); // "192.168.x.x" or "" when not connected

// Register a callback fired once when an IP is obtained.
void wifi_set_on_connected(wifi_on_connected_cb_t cb);

// Register a callback fired when a connection attempt fails due to wrong credentials.
void wifi_set_on_error(wifi_on_error_cb_t cb);

#endif
