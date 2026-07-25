#ifndef _BLE_GATT_SERVER_H_
#define _BLE_GATT_SERVER_H_

#include "esp_err.h"
#include "nvs_storage.h"

// Device status info
typedef struct {
    uint8_t battery_percent;
    uint16_t scan_count;
    char firmware_version[16];
} DeviceStatus;

// Initialize BLE GATT server
esp_err_t ble_gatt_server_init(const ScanList *scan_list, const DeviceStatus *status);
esp_err_t ble_gatt_server_update_scans(const ScanList *scan_list);
esp_err_t ble_gatt_server_deinit(void);

#endif
