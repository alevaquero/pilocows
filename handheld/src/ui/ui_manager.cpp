#include "ui_manager.h"
#include "screen_scan.h"
#include "screen_settings.h"
#include "display/display.h"
#include "esp_log.h"

static const char *TAG = "ui";
static screen_id_t s_current = SCREEN_SCAN;

void ui_manager_init(void)
{
    display_lvgl_lock();
    screen_scan_create();
    screen_settings_create();
    display_lvgl_unlock();

    ui_manager_show(SCREEN_SCAN);
    ESP_LOGI(TAG, "UI initialized");
}

void ui_manager_show(screen_id_t screen)
{
    display_lvgl_lock();
    switch (screen) {
    case SCREEN_SCAN:     screen_scan_load();     break;
    case SCREEN_SETTINGS: screen_settings_load(); break;
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
