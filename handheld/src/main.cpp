#include "board_config.h"
#include "display/display.h"
#include "rfid/rfid_reader.h"
#include "storage/scan_storage.h"
#include "ble/ble_server.h"
#include "ui/ui_manager.h"
#include "ui/screen_scan.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "peripherals/buttons.h"
#include "peripherals/buzzer.h"
#include "peripherals/vibrator.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <time.h>

static const char *TAG             = "main";
static const char *FIRMWARE_VER    = "0.1.0";

// Queue for passing RFID scan events from the RFID task to the main task
static QueueHandle_t s_scan_queue  = NULL;

// Currently selected event type (set in UI, used when saving scans)
static char s_event_type[32] = SCAN_EVENT_GENERAL;

// ---------------------------------------------------------------------------
// RFID tag callback — called from the RFID task
// ---------------------------------------------------------------------------
static void on_tag_read(const rfid_tag_t *tag)
{
    // Post to main task queue to avoid doing storage/UI work from the RFID task
    rfid_tag_t copy;
    memcpy(&copy, tag, sizeof(copy));
    xQueueSend(s_scan_queue, &copy, 0);
}

// ---------------------------------------------------------------------------
// Button callback — called from the buttons task
// ---------------------------------------------------------------------------
static void on_button(button_id_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESS) return;

    switch (btn) {
    case BTN_SCAN:
        rfid_set_scanning(true);
        ui_manager_show_status(i18n_t(STR_SCAN_SCANNING));
        break;
    case BTN_UP:
        ui_manager_show(SCREEN_SETTINGS);
        break;
    case BTN_DOWN:
        ui_manager_show(SCREEN_SCAN);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// BLE command callback — called from the BLE host task
// ---------------------------------------------------------------------------
static void on_ble_command(ble_command_t cmd, const char *payload)
{
    switch (cmd) {
    case BLE_CMD_CLEAR_LIST:
        scan_storage_clear();
        ble_server_update_status(0, FIRMWARE_VER);
        ui_manager_update_scan_count(0);
        ui_manager_show_status(i18n_t(STR_BLE_SYNC_DONE));
        break;

    case BLE_CMD_SET_TIME:
        if (payload) {
            // Parse ISO 8601 and set system time
            struct tm tm = {};
            strptime(payload, "%Y-%m-%dT%H:%M:%SZ", &tm);
            time_t t = mktime(&tm);
            struct timeval tv = { .tv_sec = t };
            settimeofday(&tv, NULL);
            ESP_LOGI(TAG, "Time set to: %s", payload);
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Main task — processes scan queue
// ---------------------------------------------------------------------------
static void main_task(void *arg)
{
    rfid_tag_t tag;

    while (true) {
        if (xQueueReceive(s_scan_queue, &tag, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Get current time as ISO 8601
            time_t now = time(NULL);
            struct tm *tm_info = gmtime(&now);
            char timestamp[25];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);

            // Build and save the scan record
            scan_record_t record = {};
            strncpy(record.eid,         tag.eid,       sizeof(record.eid) - 1);
            strncpy(record.scanned_at,  timestamp,     sizeof(record.scanned_at) - 1);
            strncpy(record.event_type,  s_event_type,  sizeof(record.event_type) - 1);

            if (scan_storage_save(&record)) {
                uint32_t count = scan_storage_count();

                // Update UI
                display_lvgl_lock();
                ui_manager_update_scan_count(count);
                screen_scan_show_tag(tag.eid, s_event_type);  // needs extern declaration
                display_lvgl_unlock();

                // Alerts
                buzzer_beep();
                vibrator_pulse();

                // Update BLE status so connected desktop sees new count
                ble_server_update_status(count, FIRMWARE_VER);
                if (ble_server_is_connected()) {
                    ble_server_notify_scan_ready();
                }

                ESP_LOGI(TAG, "Scan saved: %s [%s] count=%lu",
                         tag.eid, s_event_type, (unsigned long)count);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// app_main — ESP-IDF entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Pilocows handheld v%s starting...", FIRMWARE_VER);

    // NVS — required for BLE and settings storage
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Language preference (reads from NVS)
    i18n_init();

    // Storage (SPIFFS for scan log)
    scan_storage_init();

    // Display + Touch + LVGL
    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed — halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // UI screens
    ui_manager_init();
    ui_manager_show_status(i18n_t(STR_SCAN_READY));

    // Peripherals
    buzzer_init();
    vibrator_init();
    buttons_init(on_button);

    // Scan event queue
    s_scan_queue = xQueueCreate(16, sizeof(rfid_tag_t));

    // RFID reader
    rfid_init(on_tag_read);

    // BLE server
    ble_server_init(on_ble_command);
    ble_server_update_status(scan_storage_count(), FIRMWARE_VER);

    // Update scan count display (restore from storage after reboot)
    ui_manager_update_scan_count(scan_storage_count());

    ESP_LOGI(TAG, "Init complete — %lu scans in storage",
             (unsigned long)scan_storage_count());

    // Main processing task
    xTaskCreate(main_task, "main_task", 8192, NULL, 5, NULL);
}
