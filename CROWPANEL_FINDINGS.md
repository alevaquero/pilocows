# CrowPanel Advanced 5" ESP32-P4 — Hardware & Driver Findings

**Date**: 2026-07-24  
**Source**: `/Users/alejandro/-CrowPanel-Advanced-5inch-ESP32-P4-HMI-AI-Display-800x480-IPS-Touch-Screen/`

---

## Overview

The CrowPanel Advanced is a pre-built embedded system with:
- **Main CPU**: ESP32-P4 (RISC-V dual-core, up to 400 MHz)
- **Connectivity Module**: ESP32-C6-MINI-1 (WiFi/BLE, SDIO interface to P4)
- **Display**: 5" IPS RGB parallel (800×480, 16-bit RGB565)
- **Touch**: GT911 capacitive (I2C)
- **Onboard Management**: STC8H1KXX MCU (handles backlight, battery charging, power management)
- **Storage**: 16MB Flash, 32MB PSRAM

---

## Hardware Specifications

### Display — RGB Parallel Interface

| Parameter | Value | Notes |
|-----------|-------|-------|
| Resolution | 800×480 pixels | 16-bit RGB565 |
| Interface | RGB parallel (16-bit data) | 4 control signals + 16 data lines |
| Pixel Clock | 25 MHz | Confirmed in CrowPanel code |
| Refresh Rate | ~42 Hz | Calculated from timing |
| HSYNC pulse width | 4 cycles | Horizontal sync |
| HSYNC back porch | 8 cycles | |
| HSYNC front porch | 8 cycles | |
| VSYNC pulse width | 4 lines | Vertical sync |
| VSYNC back porch | 16 lines | |
| VSYNC front porch | 16 lines | |
| Backlight | STC8-controlled PWM | **Not directly via P4 GPIO** |
| Frame Buffer | PSRAM-based, 2 buffers | Double-buffered for smooth updates |

### RGB Control Pins

| Signal | GPIO | Direction |
|--------|------|-----------|
| PCLK (Pixel Clock) | 3 | Output |
| DE (Data Enable) | 2 | Output |
| HSYNC | 40 | Output |
| VSYNC | 41 | Output |
| DISP_EN | -1 (not used) | — |

### RGB Data Pins (16-bit)

```
DATA[0..15]:   8, 7, 6, 5, 4 | 14, 13, 12, 11, 10, 9 | 19, 18, 17, 16, 15
```

### Touch Panel — GT911

| Parameter | Value |
|-----------|-------|
| Chip | GT911 (Goodix) |
| Protocol | I2C |
| I2C Address | 0x5D (default) |
| RST Pin | GPIO 36 |
| INT Pin | GPIO 42 |
| I2C Bus | I2C0 (GPIO 45=SDA, GPIO 46=SCL) |
| Max Touches | 5-point multi-touch |
| Initialization | esp_lcd_touch_new_i2c_gt911() |

**Note**: GT911 is capacitive (unlike SC01 Plus FT6336U), but I2C protocol is similar.

### Connectivity

#### ESP32-C6-MINI-1 (WiFi/BLE Module)

Interfaced via **SDIO** (not SPI):
- SDIO SLOT 1 (4-bit mode)
- Clock: GPIO 53
- CMD: GPIO 54
- D0–D3: GPIO 52, 51, 50, 49
- Reset (GPIO 20)
- Initialized via esp_hosted framework (CONFIG_ESP_HOSTED_*)

**Phase 1 Note**: C6 is present but we'll focus on P4 only for now. Full WiFi/BLE via C6 can be added in Phase 2.

### I2C Bus Configuration

| I2C Bus | SCL | SDA | Voltage | Purpose |
|---------|-----|-----|---------|---------|
| I2C0 | 46 | 45 | 3.3V | Touch (GT911), General peripherals |
| (I2C1) | — | — | — | Not mentioned in examples |

### Power Management

- **LDO3**: 2.5V (camera, etc.)
- **LDO4**: 3.3V (I/O rail)
- Managed by ESP-IDF LDO API

### Memory

| Type | Size | Notes |
|------|------|-------|
| Flash | 16 MB | QSPI mode |
| PSRAM | 32 MB | 200 MHz mode (HEX) |
| IRAM | 768 KB (HP) + 32 KB (LP) | |

---

## Software Stack

### Firmware Build System

- **Framework**: ESP-IDF (not Arduino)
- **Build System**: CMake + idf.py
- **Components**:
  - `esp_lcd_panel_rgb` — RGB display driver
  - `esp_lcd_touch_gt911` — GT911 touch driver
  - `esp_lvgl_port` — LVGL integration layer
  - `lvgl` v8.3.11 — Graphics library

### LVGL Configuration

From `sdkconfig.defaults`:

```
CONFIG_LV_COLOR_DEPTH=16           # RGB565
CONFIG_LV_MEM_CUSTOM=y             # Use heap_caps for LVGL alloc
CONFIG_LV_MEM_SIZE_KILOBYTES=64    # Small LVGL pool (rest in PSRAM)
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_30=y
CONFIG_LV_USE_DEMO_WIDGETS=y
CONFIG_DISPLAY_LVGL_FULL_REFRESH=n # Partial refresh OK
CONFIG_DISPLAY_LVGL_DIRECT_MODE=n
CONFIG_DISPLAY_LVGL_AVOID_TEAR=y   # Avoid tearing via TE signal
```

### Display Initialization Flow

1. **LDO setup** → Enable 2.5V/3.3V regulators
2. **GPIO ISR service** → Enable interrupt handling
3. **I2C init** → Initialize I2C for touch
4. **Touch init** → Initialize GT911
5. **Display RGB init** → Configure RGB panel, allocate frame buffers
6. **LVGL port init** → Start LVGL task/FreeRTOS
7. **LVGL display add** → Register RGB display with LVGL
8. **Backlight set** → STC8 PWM command (not GPIO)

### Key API Calls (from factory code)

```c
// RGB Panel
esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
esp_lcd_panel_reset(panel_handle);
esp_lcd_panel_init(panel_handle);

// LVGL Port
lvgl_port_init(&lvgl_cfg);
lvgl_port_add_disp_rgb(&disp_cfg, &lvgl_rgb_cfg);
lvgl_port_add_touch(&touch_cfg);

// Backlight (via STC8, not direct GPIO)
stc8_set_pwm_duty(STC8_PWM_LCD_BL_EN, brightness);

// Touch
esp_lcd_touch_read_data(tp);
esp_lcd_touch_get_coordinates(tp, x, y, strength, cnt, 1);
```

---

## Differences from SC01 Plus

| Aspect | SC01 Plus | CrowPanel |
|--------|-----------|-----------|
| MCU | ESP32-S3 (all-in-one) | ESP32-P4 (compute) + C6-MINI-1 (radio) |
| Display Interface | 8080 8-bit parallel | RGB 16-bit parallel |
| Display Res | 480×320 | 800×480 |
| Touch Chip | FT6336U (resistive) | GT911 (capacitive) |
| Backlight Control | GPIO PWM (P45) | STC8 external MCU |
| Flash | 8 MB | 16 MB |
| PSRAM | 2 MB OPI | 32 MB |
| RTC | DS3231 (I2C) | (TBD—not in examples) |
| Buzzer/Vibrator | GPIO direct | (TBD—likely via STC8) |
| Build System | PlatformIO + ESP-IDF | PlatformIO + ESP-IDF (same) |

---

## Known Unknowns for Phase 1

1. **Backlight GPIO**: CrowPanel does NOT expose backlight control to P4 directly; it's via STC8 MCU. For Phase 1, we may skip or hardcode brightness.

2. **Buttons**: No traditional GPIO buttons in sample code. Need to check if user buttons are available or if we need to repurpose unused GPIO.

3. **RFID UART**: CrowPanel doesn't expose UART pins in standard examples. We may need to use:
   - UART1 (GPIO 10/11 equivalent, if available)
   - Or a different peripheral header

4. **RTC**: No DS3231 in samples. Likely available via I2C but needs confirmation.

5. **Buzzer/Vibrator**: No examples. May be controllable via:
   - GPIO (if exposed)
   - I2S (audio amplifier via onboard speaker)
   - STC8 register (if integrated)

6. **WiFi/BLE via C6**: SDIO interface works, but full integration in Phase 1 is complex. Defer to Phase 2+.

---

## Recommendations for Phase 1

### ✅ Do First
- Display RGB init → works out of the box
- Touch GT911 I2C → works out of the box
- Basic LVGL demo → tap counter example
- GPIO button input (if pins available) → simple demo

### ⏸️ Defer to Phase 2+
- RFID integration (needs UART confirmation)
- Backlight PWM (STC8 control, not straightforward)
- Buzzer/Vibrator (needs GPIO mapping)
- RTC integration (needs I2C address confirmation)
- WiFi/BLE via C6 (requires SDIO driver setup)

### 📋 Configuration Strategy

Use same pattern as SC01 Plus but ESP-IDF native:
- `board_config.h` → RGB pin defs + touch pins
- `src/display/display.cpp` → esp_lcd_rgb + LVGL init
- `src/touch/touch_gt911.cpp` → GT911 driver
- `platformio.ini` → esp32-p4 board def
- `sdkconfig.defaults` → ESP-IDF defaults (copy from CrowPanel factory)

---

## Files to Reference

| File | Location | Purpose |
|------|----------|---------|
| main.c | factory_sourcecode/.../main/ | Entry point, init flow |
| bsp_display.c | factory_sourcecode/.../peripheral/bsp_display/ | RGB + LVGL init |
| bsp_display.h | factory_sourcecode/.../peripheral/bsp_display/include/ | Pin defs + API |
| sdkconfig | factory_sourcecode/.../ | Full ESP-IDF config (post-build) |
| sdkconfig.defaults | factory_sourcecode/.../ | Curated defaults |
| Kconfig | factory_sourcecode/.../peripheral/bsp_display/ | Config menu |
