# Pilocows Handheld Porting Plan
## SC01 Plus → CrowPanel Advanced ESP32-P4

**Last Updated**: 2026-07-24  
**Status**: Planning  
**Approach**: Port-and-iterate with shared code extraction

---

## Overview

This plan covers porting the handheld firmware from SC01 Plus (ESP32-S3, 480×320 8080 parallel LCD) to CrowPanel Advanced (ESP32-P4, 800×480 RGB parallel LCD). A new `handheld_common/` will hold sharable modules, allowing both implementations to converge over time.

```
pilocows/
├── handheld/              # SC01 Plus (unchanged for now, will refactor later)
├── handheld_crowpanel/    # CrowPanel P4 (new — Phase 1 MVP)
└── handheld_common/       # Shared logic (extracted progressively)
    ├── ble/               # BLE protocol (MCU-agnostic)
    ├── rfid/              # RFID reader (MCU-agnostic)
    ├── ui/                # UI screens & logic (display-agnostic)
    ├── i18n/              # Strings & translation (shared)
    ├── storage/           # Session storage (shared)
    ├── rtc/               # RTC logic (shared)
    └── ...
```

---

## Phase 1: Hardware Drivers + Basic UI (THIS SPRINT)

### Goals
- ✅ Get display (RGB) rendering on CrowPanel
- ✅ Touch input working
- ✅ Basic button input (scan, nav buttons if available on CrowPanel)
- ✅ LVGL UI framework running
- ✅ One simple test screen (e.g., splash, demo grid)
- 📋 Document what's missing for Phase 2

### Directory Structure — `handheld_crowpanel/`

```
handheld_crowpanel/
├── platformio.ini              # P4-specific config
├── sdkconfig.defaults          # ESP-IDF config for P4
├── CMakeLists.txt
├── partitions.csv              # Flash layout (if needed)
├── VERSION                      # Firmware version (symlink or copy)
├── src/
│   ├── main.cpp                # Minimal entry point
│   ├── board_config.h          # CrowPanel pin definitions
│   ├── display/
│   │   ├── display.h           # LVGL + RGB init
│   │   ├── display.cpp
│   │   └── rgb_driver.cpp      # RGB parallel interface config
│   ├── touch/
│   │   ├── touch_gt911.h       # GT911 I2C driver
│   │   └── touch_gt911.cpp
│   ├── buttons/
│   │   ├── buttons.h
│   │   └── buttons.cpp         # GPIO input handler
│   ├── ui/
│   │   ├── ui_manager.h        # Minimal UI state
│   │   └── ui_manager.cpp
│   ├── lv_conf.h               # LVGL config (RGB-optimized)
│   └── sdkconfig_override.h    # P4-specific ESP-IDF tweaks
├── include/
│   └── fonts.h                 # Symlink to handheld/include/fonts.h (shared)
└── managed_components/         # PlatformIO-managed (LVGL, etc.)
```

### Key Implementation Details

#### `board_config.h` (CrowPanel specifics)
```cpp
// From sample code, confirmed pins:
#define LCD_RGB_HSYNC          40
#define LCD_RGB_VSYNC          41
#define LCD_RGB_DE             2
#define LCD_RGB_PCLK           3
#define LCD_RGB_DATA_PINS      {8,7,6,5,4,14,13,12,11,10,9,19,18,17,16,15}
#define LCD_H_RES              800
#define LCD_V_RES              480

#define TOUCH_PIN_SDA          45
#define TOUCH_PIN_SCL          46
#define TOUCH_PIN_INT          42
#define TOUCH_PIN_RST          36
#define TOUCH_I2C_ADDR         0x5D  // GT911 default

// CrowPanel may have different button/peripheral pins
// TBD: Check actual available headers
```

#### Display Init (`display/display.cpp`)
- Use `esp_lcd_new_rgb_panel()` instead of `esp_lcd_panel_io_i80_new()`
- Configure RGB timing (from sample: 18MHz clock, 42Hz refresh)
- Initialize LVGL with RGB framebuffer
- Handle backlight PWM (CrowPanel may differ from SC01 Plus)

#### Touch Driver (`touch/touch_gt911.cpp`)
- GT911 is capacitive (vs FT6336U resistive), but I2C protocol is similar
- Query touch data, map 800×480 coords to LVGL
- Integrate with LVGL's input device

#### Minimal Main Loop
```cpp
void app_main() {
    nvs_flash_init();
    display_init();
    touch_init();
    buttons_init();
    
    // Simple demo: draw a grid or animated logo
    // No RFID, BLE, RTC yet
    
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### Build & Flash

```bash
cd handheld_crowpanel
idf.py --port /dev/ttyUSB0 build flash monitor
# OR if using PlatformIO:
pio run -e crowpanel -t upload -t monitor
```

---

## Phase 2: Shared Code Extraction

**Goal**: Extract `handheld_common/` from both implementations, converge on shared interfaces.

### Modules to Extract (in order of priority)

| Module | Strategy | Effort |
|--------|----------|--------|
| `i18n/` | Copy as-is; both handheld/SC01 + handheld_crowpanel link to handheld_common/i18n | ⚡ Low |
| `fonts/` | Copy; may need scaling for 800×480 | ⚡ Low |
| `ble/` | Move BLE protocol logic (already MCU-agnostic) | ⚡ Low |
| `rfid/` | Move RFID reader; it only needs UART which both MCUs support | ⚡ Low |
| `storage/` | Move session storage; uses NVS/SPIFFS (both support) | ⚡ Low |
| `rtc/` | Move RTC logic; both use I2C DS3231 | ⚡ Low |
| `ui/` | Extract screen layouts & logic (hardest—must be display-agnostic) | 🔥 High |

### Abstraction Layer for Display

The trickiest part: UI code assumes a display object. Solution:

```cpp
// handheld_common/ui/lv_display_if.h
typedef struct {
    uint16_t width;
    uint16_t height;
    void (*init)(void);
    void (*flush)(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const lv_color_t *color_p);
} lv_display_if_t;

// Each handheld implementation provides:
// handheld/src/display/display_impl.cpp (8080)
// handheld_crowpanel/src/display/display_impl.cpp (RGB)
```

Then `handheld_common/ui/` calls through this interface, decoupling from transport.

---

## Phase 3: Full Feature Parity

After Phase 1 MVP, add in order:
1. RFID scanning
2. Session storage
3. BLE GATT server
4. RTC / time sync
5. Settings/language menu
6. OTA updates

---

## Implementation Checklist — Phase 1

### Hardware Driver Bring-up
- [ ] PlatformIO config for ESP32-P4 board definition
- [ ] ESP-IDF sdkconfig for RGB LCD + P4 clocks
- [ ] RGB panel init code (esp_lcd_rgb)
- [ ] LVGL + RGB driver integration
- [ ] Touch GT911 I2C driver
- [ ] Button GPIO setup
- [ ] Test: colored rect on screen, touches detected in logs

### Basic UI
- [ ] Minimal main.cpp (init display, loop)
- [ ] Simple demo screen (splash / grid / counter)
- [ ] LVGL label/button widget test
- [ ] Touch event logging

### Project Structure
- [ ] Create `handheld_crowpanel/` directory tree
- [ ] Copy sample code references (board_config.h patterns)
- [ ] `platformio.ini` for esp32-p4
- [ ] Symlink `handheld_common/i18n/` and `include/fonts.h`
- [ ] Git: document new directory in CLAUDE.md

### Documentation
- [ ] Add CrowPanel hardware pins to CLAUDE.md
- [ ] Create `docs/crowpanel_porting_notes.md` (driver mapping, known issues)
- [ ] This file (PORTING_PLAN.md)

---

## Known Unknowns (To Clarify During Implementation)

1. **ESP32-C6-MINI-1 connectivity**: Will it be auto-detected by the P4? Or do we need explicit firmware/driver?
   - If explicit: likely separate branch for C6 firmware needed
   - For Phase 1: assume only P4 is active; C6 can come later

2. **Backlight control**: CrowPanel sample shows backlight handling—need to trace which GPIO.

3. **Button availability**: CrowPanel has reset/boot buttons, but do we have 3+ user buttons like SC01 Plus?
   - If not: adjust UI navigation scheme

4. **RTC presence**: CrowPanel doesn't have DS3231 in sample; does it have onboard RTC? Or skip for Phase 1?

5. **Buzzer/vibrator**: Are these available on CrowPanel?

### Action: Before Writing Code
- [ ] Inspect CrowPanel sample code for:
  - `Lesson07-Turn_on_the_screen` → study LVGL + RGB setup
  - `Lesson08-SD_Card_File_Reading` → GPIO / peripheral patterns
  - Any example with touch → GT911 I2C init

---

## Timeline (Rough Estimates)

- **Phase 1 (HW drivers + demo UI)**: 1-2 days
  - Display init: 4-6 hrs
  - Touch + buttons: 2-3 hrs
  - Demo screen: 1 hr
  - Testing/debugging: 2-4 hrs

- **Phase 2 (Extract shared code)**: 1-2 days
  - i18n/fonts: 1 hr
  - BLE/RFID/storage: 2-3 hrs
  - UI abstraction layer: 2-3 hrs
  - Refactor original handheld: 2-4 hrs

- **Phase 3 (Full features)**: Ongoing (depends on priority)

---

## Git Strategy

1. Keep original `handheld/` untouched on `main` until Phase 2
2. Create new `handheld_crowpanel/` branch or directly on `main` (since it's a new directory)
3. Phase 2: Refactor both with shared code; commit as structured refactor

```bash
git add handheld_crowpanel/
git commit -m "add: crowpanel esp32-p4 handheld skeleton

Phase 1 MVP: hardware drivers + basic LVGL UI
- ESP32-P4 RGB display init
- GT911 touch I2C driver  
- Minimal demo screen
- Task list for Phase 2+ features"
```

---

## Success Criteria

**Phase 1 complete when:**
- ✅ Device boots, LVGL renders to display
- ✅ Touch events detected and logged
- ✅ Button presses logged
- ✅ At least one interactive demo screen (e.g., counter that increments on button)
- ✅ Task list (TASKS.md) documents missing features
- ✅ Code compiles without warnings (or annotated suppressions)
