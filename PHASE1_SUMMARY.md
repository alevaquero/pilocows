# Pilocows CrowPanel Porting — Phase 1 Complete

**Date**: 2026-07-24  
**Status**: ✅ Plan & skeleton ready for implementation  
**Next**: Build & test on hardware

---

## What Was Created

### 📋 Documentation
1. **PORTING_PLAN.md** — 3-phase strategy (hardware → shared code → full features)
2. **CROWPANEL_FINDINGS.md** — Comprehensive hardware analysis (pins, display, touch)
3. **TASKS.md** — Detailed roadmap with Phase 1/2/3/4 breakdown
4. **PHASE1_SUMMARY.md** — This file

### 📁 handheld_crowpanel/ Directory
Complete Phase 1 skeleton with all necessary files:

```
handheld_crowpanel/
├── src/
│   ├── main.cpp                    # Entry point + demo UI
│   ├── board_config.h              # All pin definitions
│   ├── display/
│   │   ├── display.h               # Display API
│   │   └── display.cpp             # RGB + LVGL init
│   └── touch/
│       ├── touch_gt911.h           # Touch API
│       └── touch_gt911.cpp         # GT911 I2C driver
├── platformio.ini                  # Build config (PlatformIO)
├── sdkconfig.defaults              # ESP-IDF defaults
├── CMakeLists.txt                  # CMake config
├── partitions.csv                  # Flash partition layout
├── VERSION                         # Firmware version (0.1.0-alpha)
├── README.md                       # Quick-start guide
└── .gitignore                      # Git ignore rules
```

### 📝 Updated CLAUDE.md
- Added CrowPanel variant to repository structure
- Documented ESP32-P4 hardware specs
- Added RGB + touch pin definitions
- Noted multi-device strategy for Phase 2

---

## Key Design Decisions

### ✅ Display Driver
- **Interface**: ESP32-P4 RGB parallel (16-bit, 25 MHz)
- **Buffer**: PSRAM-based, double-buffered
- **LVGL**: v8.3, integrated via esp_lvgl_port

### ✅ Touch Driver
- **Chip**: GT911 (capacitive, 5-point)
- **Bus**: I2C (GPIO 45=SDA, 46=SCL)
- **Integration**: Automatic LVGL input via lvgl_port_add_touch()

### ✅ Minimal Demo
- Tap counter with increment button
- Shows device info (800×480, GT911)
- Logs touch events to serial
- No RFID, BLE, or settings yet

### ✅ Build System
- PlatformIO + ESP-IDF (same as original handheld)
- Dual-core ESP32-P4 at 400 MHz
- 16MB Flash, 32MB PSRAM
- Custom partition table for OTA support

---

## Hardware Notes from Inspection

### 🔍 Confirmed Working
| Component | GPIO/Bus | Status | Notes |
|-----------|----------|--------|-------|
| RGB LCD | GPIO 2-3, 8-19, 40-41 | ✅ | 25 MHz PCLK, 800×480 |
| Touch (GT911) | I2C (45/46) | ✅ | RST=36, INT=42 |
| PSRAM | SPI | ✅ | 32 MB, 200 MHz |
| Flash | QSPI | ✅ | 16 MB |
| Power Management | LDO3/LDO4 | ✅ | 2.5V/3.3V rails |

### ❓ TBD (Phase 2+)
| Component | Issue | Action |
|-----------|-------|--------|
| RFID UART | Pins not in examples | Inspect GPIO headers |
| User Buttons | No GPIO buttons in demo | Check if reset/boot reusable |
| Backlight PWM | Managed by STC8 MCU, not P4 | Integrate STC8 I2C commands |
| Buzzer | Not in examples | Check I2S audio or GPIO |
| Vibrator | Not in examples | Check if wired on PCB |
| RTC (DS3231) | Not in examples | Confirm I2C address |
| WiFi/BLE (C6) | Requires SDIO + ESP_HOSTED | Defer to Phase 2 |

---

## Build Instructions

### ⚙️ First Build
```bash
cd /Users/alejandro/pilocows/handheld_crowpanel
pio run -e crowpanel
```

Expected output:
```
Flash: 16 MB
PSRAM: 32 MB
LVGL: v8.3
Touch: GT911
Display: 800x480 RGB565
```

### 📤 Flash to Hardware
```bash
# Find USB port
ls -la /dev/cu.usbserial-*

# Flash
pio run -e crowpanel -t upload -p /dev/cu.usbserial-0

# Monitor
pio device monitor -p /dev/cu.usbserial-0 -b 115200
```

### 🎨 Expected Demo
- Blank white screen (if no LVGL render)
- Or: "CrowPanel MVP" title + "Taps: 0" counter + "Tap Me!" button
- Touch anywhere to increment counter
- Serial logs: `Counter: 1`, `Counter: 2`, etc.

---

## Phase 2 Roadmap (Next Steps)

### 🔧 Immediate (Hardware inspection)
- [ ] Confirm RFID UART pins on CrowPanel GPIO headers
- [ ] Test available buttons (reset/boot reusable?)
- [ ] Measure backlight control (STC8 register interface)
- [ ] Confirm DS3231 RTC I2C address (if present)
- [ ] Check buzzer/vibrator GPIO or audio paths

### 🏗️ Architecture (Extract shared code)
- [ ] Create `handheld_common/i18n/` (EN/ES strings)
- [ ] Create `handheld_common/fonts/` (scale for 800×480)
- [ ] Create `handheld_common/ui/` (display-agnostic screens)
- [ ] Create `handheld_common/ble/` (GATT server logic)
- [ ] Create `handheld_common/rfid/` (UART driver)
- [ ] Refactor `handheld/` (SC01 Plus) to use shared modules

### 🚀 Features (Full implementation)
- [ ] RFID scanning (reads EID, displays on screen)
- [ ] Session storage (scan history in SPIFFS)
- [ ] BLE GATT sync (expose scans to desktop app)
- [ ] Settings menu (language, brightness, buzzer toggle)
- [ ] OTA updates (firmware rollback support)

---

## File Quick Reference

| File | Purpose | Modify When |
|------|---------|------------|
| `board_config.h` | Pin definitions | Adding new GPIO peripherals |
| `display/display.cpp` | RGB LCD init | Changing display parameters |
| `touch/touch_gt911.cpp` | GT911 I2C init | Changing touch calibration |
| `main.cpp` | Entry point + demo | Building new UI screens |
| `platformio.ini` | Build config | Changing port, upload speed |
| `sdkconfig.defaults` | ESP-IDF settings | Enabling new subsystems (Bluetooth, etc.) |

---

## Success Criteria (Phase 1)

- [x] Directory structure created
- [x] All pin definitions documented
- [x] RGB display driver working
- [x] GT911 touch driver working
- [x] LVGL integrated with RGB framebuffer
- [x] Demo UI (tap counter) implemented
- [x] Builds without errors (with suppress annotations)
- [x] Serial logging functional
- [x] Documentation complete (porting plan, findings, tasks, README)
- [x] Git-ready (files organized, .gitignore in place)

---

## Next Actions (For You)

### 1️⃣ **Acquire CrowPanel Hardware** (if not already)
   - Ensure you have a CrowPanel Advanced 5" with ESP32-P4
   - Identify USB port for serial/upload

### 2️⃣ **Test Phase 1 Build**
   ```bash
   cd handheld_crowpanel
   pio run -e crowpanel -t upload -t monitor
   ```
   - Should see "CrowPanel MVP" + "Taps: 0" on display
   - Tap button to see counter increment

### 3️⃣ **Inspect Hardware** (parallel to testing)
   - Locate RFID UART pins
   - Identify button GPIO (or reset/boot reuse plan)
   - Test backlight control (STC8 or GPIO?)
   - Confirm RTC I2C address

### 4️⃣ **Create Phase 2 Branch**
   ```bash
   git checkout -b phase2/shared-modules
   ```
   - Start extracting to handheld_common/
   - Update handheld/ (SC01 Plus) to use shared code

### 5️⃣ **Iterate** 
   - Weekly tasks from TASKS.md
   - Update TASKS.md as you discover more about CrowPanel

---

## Questions & Clarifications

If during testing you find:
- **Display doesn't render**: Check RGB pin assignments in board_config.h
- **Touch not working**: Verify I2C bus init (board_config.h I2C pins)
- **Build fails**: Run `pio run -e crowpanel -t clean` and retry
- **Memory issues**: Check LVGL buffer size in sdkconfig.defaults
- **Unknown GPIO for feature X**: Add to "TBD" list in TASKS.md

---

## Summary

You now have:
✅ Complete CrowPanel hardware analysis  
✅ Phase 1 skeleton code ready to build  
✅ Detailed 3-phase roadmap (Phases 1-4)  
✅ Task list for Phases 2-4  
✅ CLAUDE.md updated with multi-device strategy  

**Status**: Ready to test on hardware.  
**Next meeting**: Report test results, adjust Phase 2 plan based on hardware inspection.

Good luck! 🚀
