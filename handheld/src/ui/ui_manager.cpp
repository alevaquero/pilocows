#include "ui_manager.h"
#include "screen_session_menu.h"
#include "screen_session_new.h"
#include "screen_session_list.h"
#include "screen_scan.h"
#include "screen_settings.h"
#include "screen_wifi.h"
#include "screen_vaccine_settings.h"
#include "display/display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";
static screen_id_t s_current = SCREEN_SESSION_MENU;

// Create each screen under its own lock window with a brief yield between
// them. This lets IDLE0 reset the task watchdog — creating all screens in
// one shot blocks CPU 0 for longer than the 5-second WDT threshold.
#define CREATE_SCREEN(fn) \
    do { display_lvgl_lock(); fn(); display_lvgl_unlock(); \
         vTaskDelay(pdMS_TO_TICKS(5)); } while(0)

void ui_manager_init(void)
{
    CREATE_SCREEN(screen_session_menu_create);
    CREATE_SCREEN(screen_session_new_create);
    CREATE_SCREEN(screen_session_list_create);
    CREATE_SCREEN(screen_scan_create);
    CREATE_SCREEN(screen_settings_create);
    CREATE_SCREEN(screen_wifi_create);
    CREATE_SCREEN(screen_vaccine_settings_create);

    ui_manager_show(SCREEN_SESSION_MENU);
    ESP_LOGI(TAG, "UI initialized");
}

void ui_manager_show(screen_id_t screen)
{
    display_lvgl_lock();
    switch (screen) {
    case SCREEN_SESSION_MENU: screen_session_menu_load(); break;
    case SCREEN_SESSION_NEW:  screen_session_new_load();  break;
    case SCREEN_SESSION_LIST: screen_session_list_load(); break;
    case SCREEN_SCAN:         screen_scan_load();         break;
    case SCREEN_SETTINGS:     screen_settings_load();     break;
    case SCREEN_WIFI:              screen_wifi_load();              break;
    case SCREEN_VACCINE_SETTINGS:  screen_vaccine_settings_load();  break;
    default: break;
    }
    s_current = screen;
    display_lvgl_unlock();
}

void ui_manager_update_scan_count(uint32_t count)
{
    display_lvgl_lock();
    screen_scan_update_count(count);
    display_lvgl_unlock();
}

void ui_manager_show_status(const char *msg)
{
    display_lvgl_lock();
    screen_scan_show_status(msg);
    display_lvgl_unlock();
}
