#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

// Initialize LCD (ST7796UI, 8080 parallel) + Touch (FT6336U, I2C) + LVGL.
// Must be called before any LVGL code.
esp_err_t display_init(void);

// Set backlight brightness (0–100).
void display_set_brightness(uint8_t percent);

// Acquire/release the LVGL mutex before calling LVGL APIs from non-LVGL tasks.
void display_lvgl_lock(void);
void display_lvgl_unlock(void);
