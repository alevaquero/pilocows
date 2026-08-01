#include "ui_manager.h"
#include "screen_session_menu.h"
#include "screen_session_new.h"
#include "screen_session_list.h"
#include "screen_scan.h"
#include "screen_settings.h"
#include "screen_wifi.h"
#include "screen_vaccine_settings.h"
#include "screen_test_settings.h"
#include "screen_datetime.h"
#include "screen_ble_sync.h"
#include "screen_mic_test.h"
#include "screen_audio_note.h"
#include "lvgl.h"
#include "esp_log.h"

static const char *TAG = "ui_manager";
static screen_id_t s_current_screen = SCREEN_SESSION_MENU;

void ui_manager_init(void) {
    ESP_LOGI(TAG, "Initializing UI manager");

    // Create all screens once
    screen_session_menu_create();
    screen_session_new_create();
    screen_session_list_create();
    screen_scan_create();
    screen_settings_create();
    screen_wifi_create();
    screen_vaccine_settings_create();
    screen_test_settings_create();
    screen_datetime_create();
    screen_ble_sync_create();
    screen_mic_test_create();
    screen_audio_note_create();

    // Show home screen
    ui_manager_show(SCREEN_SESSION_MENU);
    ESP_LOGI(TAG, "UI manager initialized");
}

void ui_manager_show(screen_id_t screen) {
    if (screen >= SCREEN_COUNT) {
        ESP_LOGW(TAG, "Invalid screen ID: %d", screen);
        return;
    }

    s_current_screen = screen;

    switch (screen) {
        case SCREEN_SESSION_MENU:
            screen_session_menu_load();
            break;
        case SCREEN_SESSION_NEW:
            screen_session_new_load();
            break;
        case SCREEN_SESSION_LIST:
            screen_session_list_load();
            break;
        case SCREEN_SCAN:
            screen_scan_load();
            break;
        case SCREEN_SETTINGS:
            screen_settings_load();
            break;
        case SCREEN_WIFI:
            screen_wifi_load();
            break;
        case SCREEN_VACCINE_SETTINGS:
            screen_vaccine_settings_load();
            break;
        case SCREEN_TEST_SETTINGS:
            screen_test_settings_load();
            break;
        case SCREEN_DATETIME:
            screen_datetime_load();
            break;
        case SCREEN_BLE_SYNC:
            screen_ble_sync_load();
            break;
        case SCREEN_MIC_TEST:
            screen_mic_test_load();
            break;
        case SCREEN_AUDIO_NOTE:
            screen_audio_note_load();
            break;
        default:
            break;
    }

    ESP_LOGI(TAG, "Switched to screen %d", screen);
}

void ui_manager_update_scan_count(uint32_t count) {
    (void)count;
    // Update relevant screens
}

void ui_manager_show_status(const char *msg) {
    (void)msg;
    // Show transient status message
    ESP_LOGI(TAG, "Status: %s", msg);
}

void ui_manager_refresh_language(void) {
    screen_session_menu_refresh_language();
    screen_session_new_refresh_language();
    screen_session_list_refresh_language();
    screen_scan_refresh_language();
    screen_settings_refresh_language();
    screen_wifi_refresh_language();
    screen_vaccine_settings_refresh_language();
    screen_test_settings_refresh_language();
    screen_datetime_refresh_language();
    screen_ble_sync_refresh_language();
    screen_mic_test_refresh_language();
    screen_audio_note_refresh_language();
}
