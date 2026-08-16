#include <stdio.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_lvgl_port.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "nvs_flash.h"
#include "i18n.h"
#include "session_storage.h"
#include "ui_manager.h"
#include "screen_scan.h"
#include "screen_splash.h"
#include "button_driver.h"
#include "feedback_driver.h"
#include "rfid_driver.h"
#include "nvs_storage.h"
#include "ble_gatt_server.h"
#include "wifi_manager.h"
#include "ota_server.h"
#include "soft_rtc.h"
#include "app_version.h"
#include "bsp_stc8h1kxx.h"
#include "bsp_sd.h"
#include "bsp_mic.h"
#include "app_fonts.h"

static const char *TAG = "main";

// ---------------------------------------------------------------------------
// SD card required — shows a full-screen error and halts boot if the card
// isn't detected. Never returns.
//
// Diagnostic build: lvgl_port_lock(0) (wait-forever) has been observed to
// never return after a failed sd_init() — across every prior test run, the
// "lvgl_port_lock -> %d" log line never printed at all, meaning the LOCK
// ITSELF hangs, not just the rendering. Using a bounded timeout + per-step
// logging here so the next run tells us exactly where it gets stuck, instead
// of "somewhere after HALT".
// ---------------------------------------------------------------------------
static void show_sd_error_and_halt(void) {
    ESP_LOGE(TAG, "HALT: SD card not detected — insert a card and restart the device");

    while (1) {
        ESP_LOGI(TAG, "Attempting lvgl_port_lock (2s timeout)...");
        bool locked = lvgl_port_lock(2000);
        ESP_LOGI(TAG, "lvgl_port_lock -> %d", (int)locked);

        if (locked) {
            ESP_LOGI(TAG, "Creating error screen...");
            lv_obj_t *scr = lv_obj_create(NULL);
            ESP_LOGI(TAG, "lv_obj_create -> %p", (void *)scr);
            lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(scr, lv_color_hex(0xC0392B), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

            lv_obj_t *lbl = lv_label_create(scr);
            ESP_LOGI(TAG, "lv_label_create -> %p", (void *)lbl);
            lv_label_set_text(lbl, "SD card not detected.\n\nInsert a card and restart the device.");
            lv_obj_set_style_text_font(lbl, &lv_font_app_30, LV_PART_MAIN);
            lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl, 420);
            lv_obj_center(lbl);

            ESP_LOGI(TAG, "Calling lv_scr_load...");
            lv_scr_load(scr);
            ESP_LOGI(TAG, "SD error screen loaded (scr=%p, active=%p)", (void *)scr, (void *)lv_scr_act());

            lvgl_port_unlock();
            ESP_LOGI(TAG, "lvgl_port_unlock done");
            break; // screen is up — stop retrying the lock, fall through to the blight retry loop
        }

        ESP_LOGW(TAG, "lvgl_port_lock timed out — retrying");
    }

    // The STC8H1KXX management MCU (backlight over I2C) isn't reliably ready
    // to accept writes this early in boot — normally set_lcd_blight() isn't
    // called until several seconds later, after the rest of app_main() runs.
    // Its return value can't be trusted to detect failure (it reports OK
    // even when the underlying I2C write fails), so just keep retrying
    // forever instead — harmless, and self-heals once the chip is ready.
    while (1) {
        set_lcd_blight(100);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Queue for passing RFID scans from the RFID task to the main task.
static QueueHandle_t s_scan_queue = NULL;

// EID of the tag currently displayed on the scan screen (pending save).
static char s_prev_eid[SESSION_EID_MAX + 1] = {0};

// True once the scan queue has gone quiet (a read timed out) since the last
// processed scan — see main_task's debounce comment below. Starts true so
// the very first scan after boot is never suppressed.
static bool s_scan_gap_seen = true;

// ---------------------------------------------------------------------------
// RFID tag callback — called from the RFID task
// ---------------------------------------------------------------------------
static void on_tag_read(const RfidScan *scan) {
    ESP_LOGI(TAG, "on_tag_read: queueing %s", scan->eid);
    BaseType_t sent = xQueueSend(s_scan_queue, scan, 0);
    if (sent != pdTRUE) {
        ESP_LOGW(TAG, "scan queue full - tag dropped");
    }
}

// ---------------------------------------------------------------------------
// Button callback — called from the button driver task
// ---------------------------------------------------------------------------
static void on_button_event(button_id_t button, int pressed) {
    if (!pressed) return;
    ESP_LOGI(TAG, "Button pressed: %d", button);
    // RFID scanning is continuous (WL-134 reads automatically); no button
    // action is wired to it, matching the original handheld's SCAN button
    // (which was likewise a no-op — the reader triggers on its own).
}

// ---------------------------------------------------------------------------
// BLE command callback — called from the BLE host task (once ESP-HOSTED is wired)
// ---------------------------------------------------------------------------
static void on_ble_command(ble_command_t cmd, const char *payload) {
    switch (cmd) {
    case BLE_CMD_SET_TIME:
        if (payload) {
            struct tm tm = {0};
            strptime(payload, "%Y-%m-%dT%H:%M:%SZ", &tm);
            time_t t = mktime(&tm);
            soft_rtc_set_time(t);
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
//   6. Notify BLE of the updated scan count.
//
// If no session is active: display only, no storage writes, success feedback.
// ---------------------------------------------------------------------------
static void main_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "main_task started");
    RfidScan scan;

    while (true) {
        if (xQueueReceive(s_scan_queue, &scan, pdMS_TO_TICKS(100)) != pdTRUE) {
            // Nothing arrived in the last 100ms — the reader went quiet,
            // meaning the tag physically left its field (it re-transmits
            // continuously while present, with no hardware debounce — see
            // rfid_driver.c — and stops entirely once removed). This gap is
            // exactly what distinguishes "still lingering" from "the user
            // pulled it away and is about to re-present it."
            s_scan_gap_seen = true;
            continue;
        }

        const char *eid = scan.eid;

        // Without this guard, a tag lingering in the field would re-run the
        // entire display / duplicate-check / flash / sound / vibration
        // pipeline below many times per second for as long as it sits
        // there, even while the user is on a completely different screen
        // (e.g. recording a voice note) — this is what caused a crash
        // (LVGL timer/animation churn from screen_scan_flash() being
        // retriggered nonstop). Only suppress back-to-back repeats of the
        // same EID with no gap between them; the moment there's been any
        // gap since the last processed scan, treat it as fresh even if
        // it's the same tag — that's the "missed the beep, scan it again"
        // case, which always involves at least a brief gap since the
        // reader stops transmitting the instant the tag leaves range.
        if (!s_scan_gap_seen && s_prev_eid[0] != '\0' && strcmp(eid, s_prev_eid) == 0) continue;
        s_scan_gap_seen = false;

        bool has_session = false;
        {
            session_meta_t m;
            has_session = session_get_active(&m);
        }

        // 1. Auto-save previous pending tag
        if (s_prev_eid[0] != '\0' && has_session) {
            lvgl_port_lock(0);
            tag_record_t prev_rec;
            bool got = screen_scan_get_record(&prev_rec);
            lvgl_port_unlock();

            if (got) {
                session_add_tag(&prev_rec);
                uint32_t count = session_get_tag_count();
                lvgl_port_lock(0);
                screen_scan_update_count(count);
                lvgl_port_unlock();
                ESP_LOGI(TAG, "Auto-saved %s", s_prev_eid);
            }
        }

        // 2. Duplicate check
        bool duplicate = has_session && session_get_tag(eid, NULL);

        // 3. Show tag on screen
        lvgl_port_lock(0);
        screen_scan_show_tag(eid, duplicate);
        lvgl_port_unlock();

        // 4. Audio / haptic feedback
        if (duplicate) {
            ESP_LOGI(TAG, "Duplicate: %s", eid);
            sound_duplicate();
            vibrator_duplicate();
        } else {
            ESP_LOGI(TAG, "New tag: %s", eid);
            sound_new_tag();
            vibrator_success();
        }

        // 5. Track as pending
        strncpy(s_prev_eid, eid, sizeof(s_prev_eid) - 1);
        s_prev_eid[sizeof(s_prev_eid) - 1] = '\0';

        // 6. BLE status update (desktop pulls this via DEVICE_STATUS on demand)
        uint32_t count = session_get_tag_count();
        ble_gatt_server_update_status(count, FIRMWARE_VERSION);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "========== Pilocows Handheld CrowPanel ==========");
    ESP_LOGI(TAG, "Firmware version: %s", FIRMWARE_VERSION);

    // Initialize LDO power rails
    ESP_LOGI(TAG, "Initializing power management...");
    esp_ldo_channel_handle_t ldo4 = NULL;
    esp_ldo_channel_config_t ldo4_config = {
        .chan_id = 4,
        .voltage_mv = 3300,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo4_config, &ldo4);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LDO4: %s", esp_err_to_name(err));
    }

    // Initialize GPIO ISR service
    err = gpio_install_isr_service(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
    }

    // Initialize SD card (audio notes) — required, and must run BEFORE I2C,
    // touch, STC8, and the display. ESP32-P4 SDMMC slot 0's D0-D7 lines are
    // dedicated (non-GPIO-matrix) pins that can't be marked "unused" in
    // bsp_sd.c's slot_config, and this board reuses several of that range for
    // other peripherals: HSYNC=GPIO40/VSYNC=GPIO41 (display), touch INT=
    // GPIO42, I2C SDA=GPIO45/SCL=GPIO46, buzzer=GPIO47, button UP=GPIO48. A
    // failed mount's cleanup path resets ALL of D0-D7 to floating GPIOs
    // regardless of negotiated bus width — corrupting whichever of those
    // peripherals was already configured (observed as corrupted video sync,
    // a broken I2C bus, and an LVGL task hang). Running SD first means a
    // failure has nothing left to corrupt; everything else initializes fresh
    // afterward either way.
    ESP_LOGI(TAG, "Initializing SD card...");
    err = sd_init();
    if (err != ESP_OK) {
        // Minimal, first-time bring-up just for the error screen.
        i2c_init();
        stc8_i2c_init();
        display_init();
        set_lcd_blight(100);
        show_sd_error_and_halt(); // never returns
    }

    // Initialize I2C
    ESP_LOGI(TAG, "Initializing I2C...");
    err = i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Initialize touch (BEFORE display)
    ESP_LOGI(TAG, "Initializing touch...");
    err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch: %s", esp_err_to_name(err));
    }

    // Initialize STC8H1KXX management MCU (for backlight control)
    ESP_LOGI(TAG, "Initializing STC8H1KXX...");
    err = stc8_i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize STC8H1KXX: %s", esp_err_to_name(err));
    }

    // Initialize NVS — required for settings, session storage, and language
    ESP_LOGI(TAG, "Initializing NVS...");
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI(TAG, "NVS initialized successfully");

    // Language preference (reads from NVS)
    i18n_init();

    // Restore time — hardware RTC (DS3231) if one's wired up, else best-effort NVS restore.
    if (!soft_rtc_init()) {
        ESP_LOGW(TAG, "No saved time - system clock starts at epoch");
    }

    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(err));
    } else {
        // Boot splash — shown as early as possible and kept up through the
        // rest of this function's init work (session storage, mic,
        // feedback, buttons, ui_manager's screen creation, WiFi, RFID,
        // BLE), so the user sees something other than a stalled backlight
        // during that time. Backlight is turned on right away here (moved
        // up from the end of app_main()) since otherwise the splash itself
        // wouldn't be visible.
        ESP_LOGI(TAG, "Showing boot splash...");
        lvgl_port_lock(0);
        screen_splash_show();
        lvgl_port_unlock();
        set_lcd_blight(100);
    }

    // Initialize session storage (mounts SPIFFS, restores active session)
    ESP_LOGI(TAG, "Initializing session storage...");
    err = session_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize session storage: %s", esp_err_to_name(err));
    }

    // Initialize microphone (shared by the mic test screen and the audio-note
    // recorder) — non-fatal if it fails, both screens degrade to "unavailable".
    ESP_LOGI(TAG, "Initializing microphone...");
    err = mic_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Microphone unavailable: %s", esp_err_to_name(err));
    }

    // Initialize feedback drivers (buzzer + vibrator)
    ESP_LOGI(TAG, "Initializing feedback drivers...");
    err = feedback_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize feedback: %s", esp_err_to_name(err));
    }
    AppSettings settings;
    if (nvs_load_settings(&settings) == ESP_OK) {
        feedback_set_vibrator_enabled(settings.vibrator_enabled);
        feedback_set_speaker_volume(settings.speaker_volume);
        if (mic_is_ready()) mic_set_gain(settings.mic_gain);
    }

    // Initialize button driver
    ESP_LOGI(TAG, "Initializing button driver...");
    err = button_init(on_button_event);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize buttons: %s", esp_err_to_name(err));
    }

    // Initialize UI manager (must be after display + NVS + session storage)
    ESP_LOGI(TAG, "Initializing UI manager...");
    ui_manager_init();

    // WiFi — starts in background; OTA server launches on connect
    wifi_set_on_connected(ota_server_start);
    wifi_manager_init();

    // Scan event queue
    s_scan_queue = xQueueCreate(16, sizeof(RfidScan));

    // RFID reader
    ESP_LOGI(TAG, "Initializing RFID driver...");
    err = rfid_init(on_tag_read);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RFID driver: %s", esp_err_to_name(err));
    }

    // BLE GATT server — stubbed until ESP-HOSTED (ESP32-C6) bring-up
    ble_gatt_server_init(on_ble_command);
    ble_gatt_server_update_status(session_get_tag_count(), FIRMWARE_VERSION);

    // Boot complete — swap away from the splash into the real home screen.
    lvgl_port_lock(0);
    ui_manager_show(SCREEN_SESSION_MENU);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "========== System Ready ==========");
    ESP_LOGI(TAG, "%" PRIu32 " animals in active session", session_get_tag_count());
    ESP_LOGI(TAG, "Heap: %lu internal free, %lu total free",
             (unsigned long)esp_get_free_internal_heap_size(),
             (unsigned long)esp_get_free_heap_size());

    // 4096 was marginal: every scan runs its full pipeline (session lookups,
    // several nested screen_scan_* calls, ESP_LOGI string formatting, and —
    // now — LVGL animation setup for the scan flash) synchronously on this
    // stack, under lvgl_port_lock(). Bumped defensively after an
    // intermittent watchdog crash (IDLE0 starved, "Stack dump detected" —
    // ESP-IDF's panic handler couldn't cleanly unwind the captured
    // context) that a scan-heavy repro triggered; see audio_codec_util.c's
    // header comment for the same symptom class from undersized stacks
    // elsewhere in this project.
    BaseType_t task_ok = xTaskCreate(main_task, "main_task", 8192, NULL, 5, NULL);
    if (task_ok != pdPASS) {
        ESP_LOGE(TAG, "FATAL: failed to create main_task (heap exhausted?)");
    }
}
