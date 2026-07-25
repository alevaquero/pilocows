#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_ldo_regulator.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "lvgl.h"
#include "nvs_storage.h"
#include "ble_gatt_server.h"

static const char *TAG = "main";

static AppSettings settings;
static ScanList scan_list;

enum AppScreen {
    SCREEN_DEMO,
    SCREEN_SETTINGS,
    SCREEN_SCANS
};

static enum AppScreen current_screen = SCREEN_DEMO;
static int tap_count = 0;
static lv_obj_t *counter_label = NULL;

// ===== I18N Strings =====
typedef struct {
    const char *lang_en;
    const char *lang_es;
} I18nString;

#define STR(id) (strcmp(settings.language, "es") == 0 ? strings[id].lang_es : strings[id].lang_en)

enum {
    STR_TITLE,
    STR_TAP_COUNTER,
    STR_SETTINGS,
    STR_LANGUAGE,
    STR_BUZZER,
    STR_VIBRATOR,
    STR_BACK,
    STR_ENABLED,
    STR_DISABLED,
};

static I18nString strings[] = {
    {.lang_en = "Pilocows", .lang_es = "Pilocows"},
    {.lang_en = "Tap to count", .lang_es = "Toca para contar"},
    {.lang_en = "Settings", .lang_es = "Configuracion"},
    {.lang_en = "Language:", .lang_es = "Idioma:"},
    {.lang_en = "Buzzer:", .lang_es = "Zumbador:"},
    {.lang_en = "Vibrator:", .lang_es = "Vibrador:"},
    {.lang_en = "< Back", .lang_es = "< Atras"},
    {.lang_en = "ON", .lang_es = "ON"},
    {.lang_en = "OFF", .lang_es = "OFF"},
};

// ===== Forward declarations =====
static void show_demo_screen(void);
static void show_settings_screen(void);
static void show_scan_screen(void);

// ===== Tap area handlers =====
static void on_counter_tap(lv_event_t *e) {
    if (current_screen != SCREEN_DEMO) return;
    tap_count++;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\n%d", STR(STR_TAP_COUNTER), tap_count);
    lv_label_set_text(counter_label, buf);
    ESP_LOGI(TAG, "Tap count: %d", tap_count);
}

static void on_settings_tap(lv_event_t *e) {
    if (current_screen == SCREEN_DEMO) {
        show_settings_screen();
    }
}

static void on_language_tap(lv_event_t *e) {
    if (current_screen != SCREEN_SETTINGS) return;
    if (strcmp(settings.language, "en") == 0) {
        strcpy(settings.language, "es");
    } else {
        strcpy(settings.language, "en");
    }
    nvs_save_settings(&settings);
    show_settings_screen();
}

static void on_buzzer_tap(lv_event_t *e) {
    if (current_screen != SCREEN_SETTINGS) return;
    settings.buzzer_enabled = !settings.buzzer_enabled;
    nvs_save_settings(&settings);
    show_settings_screen();
}

static void on_vibrator_tap(lv_event_t *e) {
    if (current_screen != SCREEN_SETTINGS) return;
    settings.vibrator_enabled = !settings.vibrator_enabled;
    nvs_save_settings(&settings);
    show_settings_screen();
}

static void on_clear_scans_tap(lv_event_t *e) {
    if (current_screen != SCREEN_SETTINGS) return;
    nvs_clear_scans();
    scan_list.count = 0;
    show_settings_screen();
}

static void on_back_tap(lv_event_t *e) {
    if (current_screen == SCREEN_SETTINGS || current_screen == SCREEN_SCANS) {
        show_demo_screen();
    }
}

static void on_scans_tap(lv_event_t *e) {
    if (current_screen == SCREEN_DEMO) {
        show_scan_screen();
    }
}

// ===== Screen creation =====
static void show_demo_screen(void) {
    current_screen = SCREEN_DEMO;
    lv_obj_clean(lv_scr_act());

    // Background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);

    // Title
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, STR(STR_TITLE));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

    // Counter label (large tappable area)
    counter_label = lv_label_create(lv_scr_act());
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\n%d", STR(STR_TAP_COUNTER), tap_count);
    lv_label_set_text(counter_label, buf);
    lv_obj_center(counter_label);
    lv_obj_add_flag(counter_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(counter_label, on_counter_tap, LV_EVENT_CLICKED, NULL);

    // Scan count info (tappable to view scans)
    lv_obj_t *scan_info = lv_label_create(lv_scr_act());
    char scan_buf[32];
    snprintf(scan_buf, sizeof(scan_buf), "Scans: %d", scan_list.count);
    lv_label_set_text(scan_info, scan_buf);
    lv_obj_align(scan_info, LV_ALIGN_BOTTOM_LEFT, 10, -20);
    lv_obj_add_flag(scan_info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scan_info, on_scans_tap, LV_EVENT_CLICKED, NULL);

    // Settings label (bottom right, tappable)
    lv_obj_t *settings_label = lv_label_create(lv_scr_act());
    lv_label_set_text(settings_label, STR(STR_SETTINGS));
    lv_obj_align(settings_label, LV_ALIGN_BOTTOM_RIGHT, -10, -20);
    lv_obj_add_flag(settings_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(settings_label, on_settings_tap, LV_EVENT_CLICKED, NULL);
}

static void show_settings_screen(void) {
    current_screen = SCREEN_SETTINGS;
    lv_obj_clean(lv_scr_act());

    // Background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);

    // Title
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, STR(STR_SETTINGS));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    int y_pos = 50;
    int item_height = 45;

    // Language
    lv_obj_t *lang_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lang_container, 300, 40);
    lv_obj_set_pos(lang_container, 10, y_pos);
    lv_obj_set_style_bg_color(lang_container, lv_color_hex(0xf0f0f0), 0);
    lv_obj_add_flag(lang_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lang_container, on_language_tap, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lang_text = lv_label_create(lang_container);
    char lang_buf[64];
    snprintf(lang_buf, sizeof(lang_buf), "%s %s", STR(STR_LANGUAGE),
             strcmp(settings.language, "es") == 0 ? "ES" : "EN");
    lv_label_set_text(lang_text, lang_buf);
    lv_obj_center(lang_text);

    y_pos += item_height;

    // Buzzer
    lv_obj_t *buzzer_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(buzzer_container, 300, 40);
    lv_obj_set_pos(buzzer_container, 10, y_pos);
    lv_obj_set_style_bg_color(buzzer_container, lv_color_hex(0xf0f0f0), 0);
    lv_obj_add_flag(buzzer_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(buzzer_container, on_buzzer_tap, LV_EVENT_CLICKED, NULL);

    lv_obj_t *buzzer_text = lv_label_create(buzzer_container);
    char buzzer_buf[64];
    snprintf(buzzer_buf, sizeof(buzzer_buf), "%s %s", STR(STR_BUZZER),
             settings.buzzer_enabled ? STR(STR_ENABLED) : STR(STR_DISABLED));
    lv_label_set_text(buzzer_text, buzzer_buf);
    lv_obj_center(buzzer_text);

    y_pos += item_height;

    // Vibrator
    lv_obj_t *vib_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(vib_container, 300, 40);
    lv_obj_set_pos(vib_container, 10, y_pos);
    lv_obj_set_style_bg_color(vib_container, lv_color_hex(0xf0f0f0), 0);
    lv_obj_add_flag(vib_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(vib_container, on_vibrator_tap, LV_EVENT_CLICKED, NULL);

    lv_obj_t *vib_text = lv_label_create(vib_container);
    char vib_buf[64];
    snprintf(vib_buf, sizeof(vib_buf), "%s %s", STR(STR_VIBRATOR),
             settings.vibrator_enabled ? STR(STR_ENABLED) : STR(STR_DISABLED));
    lv_label_set_text(vib_text, vib_buf);
    lv_obj_center(vib_text);

    y_pos += item_height;

    // Clear scans button
    lv_obj_t *clear_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(clear_container, 300, 40);
    lv_obj_set_pos(clear_container, 10, y_pos);
    lv_obj_set_style_bg_color(clear_container, lv_color_hex(0xff6b6b), 0);
    lv_obj_add_flag(clear_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(clear_container, on_clear_scans_tap, LV_EVENT_CLICKED, NULL);

    lv_obj_t *clear_text = lv_label_create(clear_container);
    char clear_buf[64];
    snprintf(clear_buf, sizeof(clear_buf), "Clear Scans (%d)", scan_list.count);
    lv_label_set_text(clear_text, clear_buf);
    lv_obj_center(clear_text);

    // Back button
    lv_obj_t *back_label = lv_label_create(lv_scr_act());
    lv_label_set_text(back_label, STR(STR_BACK));
    lv_obj_align(back_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_label, on_back_tap, LV_EVENT_CLICKED, NULL);
}

static void show_scan_screen(void) {
    current_screen = SCREEN_SCANS;
    lv_obj_clean(lv_scr_act());

    // Background
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_white(), 0);

    // Title
    lv_obj_t *title = lv_label_create(lv_scr_act());
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "Scans: %d", scan_list.count);
    lv_label_set_text(title, title_buf);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Scan list container with scroll
    lv_obj_t *list_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(list_container, 320, 380);
    lv_obj_set_pos(list_container, 0, 40);
    lv_obj_set_style_bg_color(list_container, lv_color_white(), 0);
    lv_obj_set_style_border_width(list_container, 1, 0);
    lv_obj_set_scroll_dir(list_container, LV_DIR_VER);

    if (scan_list.count == 0) {
        lv_obj_t *empty_label = lv_label_create(list_container);
        lv_label_set_text(empty_label, "No scans yet");
        lv_obj_center(empty_label);
    } else {
        // Display scans (show last 10 to avoid clutter)
        int start_idx = scan_list.count > 10 ? scan_list.count - 10 : 0;
        int y_offset = 0;

        for (int i = start_idx; i < scan_list.count; i++) {
            lv_obj_t *scan_item = lv_obj_create(list_container);
            lv_obj_set_size(scan_item, 300, 45);
            lv_obj_set_pos(scan_item, 5, y_offset);
            lv_obj_set_style_bg_color(scan_item, lv_color_hex(0xf5f5f5), 0);
            lv_obj_set_style_border_width(scan_item, 0, 0);
            lv_obj_set_style_pad_all(scan_item, 5, 0);

            char scan_text[64];
            snprintf(scan_text, sizeof(scan_text), "EID: %s", scan_list.scans[i].eid);

            lv_obj_t *eid_label = lv_label_create(scan_item);
            lv_label_set_text(eid_label, scan_text);
            lv_obj_set_width(eid_label, 290);
            lv_label_set_long_mode(eid_label, LV_LABEL_LONG_WRAP);
            lv_obj_center(eid_label);

            y_offset += 50;
        }

        // Scroll to bottom to show latest scans
        lv_obj_scroll_to_view_recursive(list_container, LV_ANIM_OFF);
    }

    // Back button
    lv_obj_t *back_label = lv_label_create(lv_scr_act());
    lv_label_set_text(back_label, STR(STR_BACK));
    lv_obj_align(back_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_add_flag(back_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(back_label, on_back_tap, LV_EVENT_CLICKED, NULL);
}

// ===== Main app =====
void app_main(void)
{
    ESP_LOGI(TAG, "========== Pilocows Handheld CrowPanel ==========");
    ESP_LOGI(TAG, "Firmware version: 0.1.0-alpha");

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

    // Initialize I2C (needed for touch)
    ESP_LOGI(TAG, "Initializing I2C...");
    err = i2c_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(200));

    // Initialize touch BEFORE display (critical for touch to work!)
    ESP_LOGI(TAG, "Initializing touch...");
    err = touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch: %s", esp_err_to_name(err));
    }

    // Initialize display
    ESP_LOGI(TAG, "Initializing display...");
    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display initialization failed: %s", esp_err_to_name(err));
    }

    // Load settings and scans from NVS
    ESP_LOGI(TAG, "Loading settings and scan history from NVS...");
    nvs_load_settings(&settings);
    nvs_load_scans(&scan_list);

    // Add mock scan data for testing (remove when RFID driver is implemented)
    if (scan_list.count == 0) {
        ESP_LOGI(TAG, "Adding mock scan data for testing...");
        strncpy(scan_list.scans[0].eid, "98765432101", sizeof(scan_list.scans[0].eid) - 1);
        scan_list.scans[0].timestamp = 1000;
        strncpy(scan_list.scans[1].eid, "12345678901", sizeof(scan_list.scans[1].eid) - 1);
        scan_list.scans[1].timestamp = 2000;
        strncpy(scan_list.scans[2].eid, "55555555555", sizeof(scan_list.scans[2].eid) - 1);
        scan_list.scans[2].timestamp = 3000;
        scan_list.count = 3;
    }

    // Initialize BLE GATT server
    ESP_LOGI(TAG, "Initializing BLE GATT server...");
    DeviceStatus device_status = {
        .battery_percent = 100,
        .scan_count = scan_list.count,
        .firmware_version = "0.1.0-alpha"
    };
    err = ble_gatt_server_init(&scan_list, &device_status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BLE: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "========== System Ready ==========");
    ESP_LOGI(TAG, "Language: %s, Buzzer: %s, Vibrator: %s, Scans: %d",
             settings.language, settings.buzzer_enabled ? "ON" : "OFF",
             settings.vibrator_enabled ? "ON" : "OFF", scan_list.count);

    // Debug: Log current scan list
    for (int i = 0; i < scan_list.count; i++) {
        ESP_LOGI(TAG, "  Scan[%d]: %s @ %u", i, scan_list.scans[i].eid, scan_list.scans[i].timestamp);
    }

    // Show demo screen
    show_demo_screen();

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "running... Settings: lang=%s, buzzer=%d, vib=%d",
                 settings.language, settings.buzzer_enabled, settings.vibrator_enabled);
    }
}
