#include "rfid_driver.h"
#include "esp_log.h"
#include "driver/uart.h"
#include <string.h>

static const char *TAG = "rfid";
static rfid_callback_t s_callback = NULL;
static uint32_t s_scan_count = 0;

// Phase 2+: RFID UART implementation
// Hardware: 134.2K AGV FDX-B module on GPIO 10 (RX) / GPIO 11 (TX)
// When hardware is connected:
// 1. Configure UART1 for the module (typical 9600 baud)
// 2. Implement frame parsing (EID extraction from module protocol)
// 3. Validate EID format (11 digits)
// 4. Call callback on successful scan
// 5. Add NVS persistence via nvs_storage.c

esp_err_t rfid_init(rfid_callback_t callback) {
    if (!callback) return ESP_ERR_INVALID_ARG;

    s_callback = callback;
    s_scan_count = 0;

    ESP_LOGI(TAG, "RFID driver initialized (Phase 2+ - awaiting hardware on GPIO 10/11)");
    ESP_LOGI(TAG, "Expected protocol: FDX-B 134.2K ISO 11784/85");
    ESP_LOGI(TAG, "Ready to receive 11-digit EIDs via UART");

    return ESP_OK;
}

esp_err_t rfid_deinit(void) {
    ESP_LOGI(TAG, "RFID driver deinitialized");
    s_callback = NULL;
    return ESP_OK;
}

uint32_t rfid_get_scan_count(void) {
    return s_scan_count;
}

void rfid_clear_scans(void) {
    s_scan_count = 0;
    ESP_LOGI(TAG, "Scans cleared");
}

// TODO: Implement UART receive task
// - Configure UART1 with ISO 11784/85 framing
// - Parse incoming EID from module protocol
// - Validate 11-digit format
// - Call callback with RfidScan data
// - Update s_scan_count
// - Handle errors and resync on protocol violations

// TODO: Add to main.c
// - Call rfid_init() during app_main after display init
// - Pass callback that updates UI via screen manager
// - Integrate with NVS persistence layer
