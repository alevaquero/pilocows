#include "display.h"
#include "board_config.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lvgl_port.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t   s_panel      = NULL;
static esp_lcd_touch_handle_t   s_touch      = NULL;
static lv_disp_t               *s_lvgl_disp  = NULL;

// ---------------------------------------------------------------------------
// Backlight — LEDC PWM on LCD_PIN_BL
// ---------------------------------------------------------------------------
static void backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 5000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num   = LCD_PIN_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel);
}

void display_set_brightness(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = ((1 << 10) - 1) * percent / 100;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

// ---------------------------------------------------------------------------
// I2C bus for touch
// ---------------------------------------------------------------------------
static esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_PIN_SDA,
        .scl_io_num       = TOUCH_PIN_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master = {
            .clk_speed    = TOUCH_I2C_FREQ_HZ,
        },
    };
    esp_err_t err = i2c_param_config(TOUCH_I2C_PORT, &conf);
    if (err != ESP_OK) return err;
    return i2c_driver_install(TOUCH_I2C_PORT, conf.mode, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// LCD init — 8080 parallel bus → ST7796UI panel
// ---------------------------------------------------------------------------
static esp_err_t lcd_init(esp_lcd_panel_io_handle_t *io_out)
{
    esp_err_t err;

    // Reset LCD + touch together (shared pin GPIO 4)
    gpio_config_t rst_cfg = {
        .pin_bit_mask = BIT64(LCD_PIN_RESET),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst_cfg);
    gpio_set_level((gpio_num_t)LCD_PIN_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)LCD_PIN_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Create I80 (8080) bus
    esp_lcd_i80_bus_handle_t i80_bus;
    esp_lcd_i80_bus_config_t bus_cfg = {
        .dc_gpio_num     = LCD_PIN_RS,
        .wr_gpio_num     = LCD_PIN_WR,
        .clk_src         = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums  = {
            LCD_PIN_DB0, LCD_PIN_DB1, LCD_PIN_DB2, LCD_PIN_DB3,
            LCD_PIN_DB4, LCD_PIN_DB5, LCD_PIN_DB6, LCD_PIN_DB7,
        },
        .bus_width       = 8,
        .max_transfer_bytes = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    err = esp_lcd_new_i80_bus(&bus_cfg, &i80_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I80 bus: %s", esp_err_to_name(err));
        return err;
    }

    // Create panel IO on the I80 bus
    // Field order must match struct declaration in ESP-IDF 5.4:
    // cs_gpio_num, pclk_hz, trans_queue_depth, on_color_trans_done, user_ctx,
    // lcd_cmd_bits, lcd_param_bits, flags, dc_levels
    esp_lcd_panel_io_i80_config_t io_cfg = {};
    io_cfg.cs_gpio_num       = -1;
    io_cfg.pclk_hz           = LCD_PCLK_HZ;
    io_cfg.trans_queue_depth = 10;
    io_cfg.lcd_cmd_bits      = 8;
    io_cfg.lcd_param_bits    = 8;
    io_cfg.dc_levels.dc_idle_level  = 0;
    io_cfg.dc_levels.dc_cmd_level   = 0;
    io_cfg.dc_levels.dc_dummy_level = 0;
    io_cfg.dc_levels.dc_data_level  = 1;
    io_cfg.flags.swap_color_bytes   = 1;  // TODO: color mapping under investigation — see color test screen.
    err = esp_lcd_new_panel_io_i80(i80_bus, &io_cfg, io_out);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(err));
        return err;
    }

    // Create ST7796 panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num   = -1,          // Reset already done manually above
        .rgb_ele_order    = LCD_RGB_ELEMENT_ORDER_BGR, // SC01 Plus: MADCTL BGR=1; LVGL sends RGB so driver must swap
        .bits_per_pixel   = LCD_BITS_PER_PIXEL,
    };
    err = esp_lcd_new_panel_st7796(*io_out, &panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ST7796 panel: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);

    // SC01 Plus: landscape orientation, origin top-left
    esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_mirror(s_panel, false, false);
    // SC01 Plus uses an IPS panel: native pixel polarity is inverted, so INVON
    // is required to get correct colors. Without it every color appears as NOT(intended).
    esp_lcd_panel_invert_color(s_panel, true);
    esp_lcd_panel_disp_on_off(s_panel, true);

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Touch init — FT6336U via I2C (compatible with FT5x06 driver)
// ---------------------------------------------------------------------------
static esp_err_t touch_init(esp_lcd_panel_io_handle_t lcd_io)
{
    (void)lcd_io;  // Touch uses its own I2C bus, not the LCD IO

    // Avoid ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG() macro — it uses out-of-order
    // designated initializers that break C++ compilation with GCC on ESP-IDF 5.4.
    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = {};
    tp_io_cfg.dev_addr            = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
    tp_io_cfg.control_phase_bytes = 1;
    tp_io_cfg.dc_bit_offset       = 0;
    tp_io_cfg.lcd_cmd_bits        = 8;
    tp_io_cfg.lcd_param_bits      = 0;
    tp_io_cfg.flags.disable_control_phase = 1;
    esp_err_t err = esp_lcd_new_panel_io_i2c(
        (esp_lcd_i2c_bus_handle_t)TOUCH_I2C_PORT,
        &tp_io_cfg,
        &tp_io
    );
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch IO: %s", esp_err_to_name(err));
        return err;
    }

    // FT6336U on SC01 Plus reports portrait coords: x_raw [0,320], y_raw [0,480].
    // Transform order in esp_lcd_touch: mirror_x → mirror_y → swap_xy.
    // We need: x_out = y_raw [0,480], y_out = (320 - x_raw) [0,320].
    // Before swap: x_pre = (320 - x_raw) via mirror_x(x_max=320), y_pre = y_raw (no mirror).
    // After swap:  x_out = y_pre = y_raw, y_out = x_pre = 320 - x_raw. ✓
    esp_lcd_touch_config_t tp_cfg = {
        .x_max         = LCD_V_RES,     // IC portrait x range (320); used by mirror_x
        .y_max         = LCD_H_RES,     // IC portrait y range (480); unused (mirror_y=0)
        .rst_gpio_num  = GPIO_NUM_NC,   // Reset already done with LCD
        .int_gpio_num  = (gpio_num_t)TOUCH_PIN_INT,
        .levels = {
            .reset     = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy   = 1,
            .mirror_x  = 1,
            .mirror_y  = 0,
        },
    };
    err = esp_lcd_touch_new_i2c_ft5x06(tp_io, &tp_cfg, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init FT6336U touch: %s", esp_err_to_name(err));
    }
    return err;
}

// ---------------------------------------------------------------------------
// LVGL port init — creates LVGL task, registers display + touch
// ---------------------------------------------------------------------------
static esp_err_t lvgl_init(esp_lcd_panel_io_handle_t lcd_io)
{
    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t err = lvgl_port_init(&port_cfg);
    if (err != ESP_OK) return err;

    // Register display
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle     = lcd_io,
        .panel_handle  = s_panel,
        .buffer_size   = LCD_H_RES * 20,  // 20-line draw buffer (~19KB) — save internal DMA RAM
        .double_buffer = true,
        .hres          = LCD_H_RES,
        .vres          = LCD_V_RES,
        .monochrome    = false,
        .rotation = {
            .swap_xy   = false,
            .mirror_x  = false,
            .mirror_y  = false,
        },
        .flags = {
            .buff_dma  = true,
        },
    };
    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (!s_lvgl_disp) {
        ESP_LOGE(TAG, "Failed to add LVGL display");
        return ESP_FAIL;
    }

    // Register touch input
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp   = s_lvgl_disp,
        .handle = s_touch,
    };
    if (!lvgl_port_add_touch(&touch_cfg)) {
        ESP_LOGE(TAG, "Failed to add LVGL touch");
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t display_init(void)
{
    esp_err_t err;

    backlight_init();
    display_set_brightness(0);  // Off during init to avoid flicker

    err = i2c_bus_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t lcd_io = NULL;
    err = lcd_init(&lcd_io);
    if (err != ESP_OK) return err;

    err = touch_init(lcd_io);
    if (err != ESP_OK) return err;

    err = lvgl_init(lcd_io);
    if (err != ESP_OK) return err;

    display_set_brightness(80);  // Default brightness
    ESP_LOGI(TAG, "Display init OK — %dx%d ST7796UI + FT6336U", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

void display_lvgl_lock(void)
{
    lvgl_port_lock(0);
}

void display_lvgl_unlock(void)
{
    lvgl_port_unlock();
}
