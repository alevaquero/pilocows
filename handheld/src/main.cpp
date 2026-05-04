#include "board_config.h"
#include "display/display.h"
#include "rfid/rfid_reader.h"
#include "storage/session_storage.h"
#include "ble/ble_server.h"
#include "ui/ui_manager.h"
#include "ui/screen_scan.h"
#include "i18n/i18n.h"
#include "i18n/strings_en.h"
#include "peripherals/buttons.h"
#include "peripherals/buzzer.h"
#include "peripherals/vibrator.h"
#include "rtc/rtc.h"
#include "wifi/wifi_manager.h"
#include "ota/ota_server.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <time.h>

static const char *TAG          = "main";
static const char *FIRMWARE_VER = FIRMWARE_VERSION;  // injected by version_flag.py from handheld/VERSION

// Queue for passing RFID events from the RFID task to the main task.
static QueueHandle_t s_scan_queue = NULL;

// EID of the tag currently displayed on the scan screen (pending save).
// Empty string means no pending tag.
static char s_prev_eid[SESSION_EID_MAX + 1] = {0};

// ---------------------------------------------------------------------------
// RFID tag callback — called from the RFID task
// ---------------------------------------------------------------------------
static void on_tag_read(const rfid_tag_t *tag)
{
    ESP_LOGI(TAG, "on_tag_read: queueing %s", tag->eid);
    BaseType_t sent = xQueueSend(s_scan_queue, tag, 0);
    if (sent != pdTRUE) {
        ESP_LOGW(TAG, "scan queue full — tag dropped");
    }
}

// ---------------------------------------------------------------------------
// Button callback — called from the buttons task
// ---------------------------------------------------------------------------
static void on_button(button_id_t btn, button_event_t event)
{
    if (event != BTN_EVENT_PRESS) return;
    switch (btn) {
    case BTN_SCAN:
        // RFID reader handles scan-on-press directly; nothing extra needed here.
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

    case BLE_CMD_SET_TIME:
        if (payload) {
            struct tm tm = {};
            strptime(payload, "%Y-%m-%dT%H:%M:%SZ", &tm);
            time_t t = mktime(&tm);
            struct timeval tv = { .tv_sec = t };
            settimeofday(&tv, NULL);
            rtc_set_time(t);
            ESP_LOGI(TAG, "Time set to: %s", payload);
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// Main task — session-aware scan processing
//
// Flow for each received tag:
//   1. Auto-save the previously shown tag (if any) to session storage.
//   2. Check whether this EID already exists in the active session.
//   3. Show the tag on screen (green=new, red=duplicate; pre-fills fields if duplicate).
//   4. Feedback: success beep/vibrate for new, duplicate beep/vibrate for re-scan.
//   5. Track the new EID as the current pending tag.
//   6. Notify BLE if connected.
//
// If no session is active: display only, no storage writes, success feedback.
// ---------------------------------------------------------------------------
static void main_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "main_task started");
    rfid_tag_t tag;

    while (true) {
        if (xQueueReceive(s_scan_queue, &tag, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        const char *eid = tag.eid;
        bool has_session = false;
        {
            session_meta_t m;
            has_session = session_get_active(&m);
        }

        // ── 1. Auto-save previous pending tag ────────────────────────────────
        if (s_prev_eid[0] != '\0' && has_session) {
            // Read current UI field values (must be under LVGL lock)
            display_lvgl_lock();
            tag_record_t prev_rec;
            bool got = screen_scan_get_record(&prev_rec);
            display_lvgl_unlock();

            if (got) {
                session_save_record(&prev_rec);
                uint32_t count = session_get_tag_count();
                display_lvgl_lock();
                screen_scan_update_count(count);
                display_lvgl_unlock();
                ESP_LOGI(TAG, "Auto-saved %s", s_prev_eid);
            }
        }

        // ── 2. Duplicate check (is this EID already saved in the session?) ───
        // session_find_record returns false if no session is active.
        bool duplicate = session_find_record(eid, NULL);

        // ── 3. Show tag on screen ─────────────────────────────────────────────
        display_lvgl_lock();
        screen_scan_show_tag(eid, duplicate);
        display_lvgl_unlock();

        // ── 4. Audio / haptic feedback ────────────────────────────────────────
        if (duplicate) {
            ESP_LOGI(TAG, "Duplicate: %s", eid);
            buzzer_duplicate();
            vibrator_duplicate();
        } else {
            ESP_LOGI(TAG, "New tag:   %s", eid);
            buzzer_success();
            vibrator_success();
        }

        // ── 5. Track as pending ───────────────────────────────────────────────
        strlcpy(s_prev_eid, eid, sizeof(s_prev_eid));

        // ── 6. BLE notification ───────────────────────────────────────────────
        uint32_t count = session_get_tag_count();
        ble_server_update_status(count, FIRMWARE_VER);
        if (ble_server_is_connected()) {
            ble_server_notify_scan_ready();
        }
    }
}

// ---------------------------------------------------------------------------
// app_main — ESP-IDF entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Pilocows handheld v%s starting...", FIRMWARE_VER);

    // NVS — required for BLE, WiFi, settings and session storage
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Language preference (reads from NVS)
    i18n_init();

    // Session storage — mounts SPIFFS and restores active session from NVS
    session_storage_init();

    // RTC — installs its own I2C driver (I2C_NUM_1, GPIO 13/14)
    if (!rtc_init()) {
        ESP_LOGW(TAG, "DS3231 not found — system clock starts at epoch");
    }

    // Display + Touch + LVGL — must come before WiFi so the two 38KB DMA
    // draw buffers are allocated from the full heap before the WiFi stack
    // claims its share (~40-80 KB).
    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed — halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // UI screens — starts on session menu
    ui_manager_init();

    // WiFi — starts in background; OTA server launches on connect
    wifi_set_on_connected(ota_server_start);
    wifi_manager_init();

    // Peripherals
    buzzer_init();
    vibrator_init();
    buttons_init(on_button);

    // Scan event queue
    s_scan_queue = xQueueCreate(16, sizeof(rfid_tag_t));

    // RFID reader
    rfid_init(on_tag_read);

    // BLE server — report current session animal count
    uint32_t initial_count = session_get_tag_count();
    ble_server_init(on_ble_command);
    ble_server_update_status(initial_count, FIRMWARE_VER);

    ESP_LOGI(TAG, "Init complete — %" PRIu32 " animals in active session", initial_count);
    ESP_LOGI(TAG, "Heap: %lu internal free, %lu total free",
             (unsigned long)esp_get_free_internal_heap_size(),
             (unsigned long)esp_get_free_heap_size());

    // Main processing task — stack uses PSRAM fallback when ALWAYSINTERNAL < stack size
    BaseType_t task_ok = xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "FATAL: failed to create main_task (heap exhausted?)");
    }
}
