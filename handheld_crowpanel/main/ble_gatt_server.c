#include "ble_gatt_server.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ble_gatt";

// Phase 2+ Note: BLE requires ESP32-C6-MINI-1 connectivity module via ESP_HOSTED framework
// For now, we stub the interface to allow NVS storage to work independently

esp_err_t ble_gatt_server_init(const ScanList *scan_list, const DeviceStatus *status) {
    if (!scan_list || !status) return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "BLE GATT server (Phase 2+ - requires ESP32-C6 connectivity module)");
    ESP_LOGI(TAG, "Status: Device has %d scans, battery: %d%%, fw: %s",
             scan_list->count, status->battery_percent, status->firmware_version);
    ESP_LOGI(TAG, "BLE will be enabled in Phase 2 with ESP_HOSTED framework");

    return ESP_OK;
}

esp_err_t ble_gatt_server_update_scans(const ScanList *scan_list) {
    if (!scan_list) return ESP_ERR_INVALID_ARG;
    ESP_LOGD(TAG, "Scan list update: %d scans", scan_list->count);
    return ESP_OK;
}

esp_err_t ble_gatt_server_deinit(void) {
    ESP_LOGI(TAG, "BLE GATT server deinit");
    return ESP_OK;
}
