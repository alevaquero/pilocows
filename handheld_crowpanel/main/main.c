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

static const char *TAG = "main";

// Settings and UI state
typedef struct {
    char language[3];  // "en" or "es"
    bool buzzer_enabled;
    bool vibrator_enabled;
} AppSettings;

static AppSettings settings = {
    .language = "en",
    .buzzer_enabled = true,
    .vibrator_enabled = true
};

enum AppScreen {
    SCREEN_DEMO,
    SCREEN_SETTINGS
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
    show_settings_screen();
}

static void on_buzzer_tap(lv_event_t *e) {
    if (current_screen != SCREEN_SETTINGS) return;
    settings.buzzer_enabled = !settings.buzzer_enabled;
    show_settings_screen();
}

static void on_vibrator_tap(lv_event_t *e) {
    if (current_screen != SCREEN_SETTINGS) return;
    settings.vibrator_enabled = !settings.vibrator_enabled;
    show_settings_screen();
}

static void on_back_tap(lv_event_t *e) {
    if (current_screen == SCREEN_SETTINGS) {
        show_demo_screen();
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

    // Settings label (bottom, tappable)
    lv_obj_t *settings_label = lv_label_create(lv_scr_act());
    lv_label_set_text(settings_label, STR(STR_SETTINGS));
    lv_obj_align(settings_label, LV_ALIGN_BOTTOM_MID, 0, -20);
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

    ESP_LOGI(TAG, "========== System Ready ==========");

    // Show demo screen
    show_demo_screen();

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "running... Settings: lang=%s, buzzer=%d, vib=%d",
                 settings.language, settings.buzzer_enabled, settings.vibrator_enabled);
    }
}
