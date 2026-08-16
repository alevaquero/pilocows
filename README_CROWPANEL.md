# Pilocows Handheld — CrowPanel Advanced 5" ESP32-P4 Build Guide

> **⚠️ Superseded.** This doc describes the Phase 1 MVP (PlatformIO, demo tap-counter UI).
> The project has since moved to plain ESP-IDF (`idf.py`, not PlatformIO) and is now in
> Phase 2. For current build/flash instructions, see the **Handheld** section of the
> root [`README.md`](README.md). Kept here for historical reference only.

**Status**: Phase 1 MVP (Display + Touch drivers, demo UI) — historical

---

## Hardware

| Component | Spec |
|---|---|
| MCU | ESP32-P4 (RISC-V dual-core, 400 MHz) |
| Connectivity | ESP32-C6-MINI-1 (WiFi/BLE via SDIO, Phase 2+) |
| Display | 5" IPS RGB parallel, 800×480, 16-bit RGB565 |
| Touch | GT911 capacitive (5-point), I2C |
| Memory | 16 MB Flash (QIO), 32 MB PSRAM (200 MHz) |
| Power | USB-C (5V/2A) |

---

## Prerequisites

- [VS Code](https://code.visualstudio.com/) + [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- **OR** [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/)
- Python 3.8+ (included with PlatformIO)
- USB-C cable connected to CrowPanel

### Identify USB Port

```bash
# macOS / Linux
ls /dev/cu.usbserial-* /dev/ttyUSB*

# Example output
/dev/cu.usbserial-0
/dev/cu.usbserial-1

# Pick the one that appears when CrowPanel is connected
```

If your USB port differs from `/dev/cu.usbserial-0`, update `platformio.ini`:

```ini
# handheld_crowpanel/platformio.ini
upload_port = /dev/cu.usbserial-0  ← Change this
monitor_port = /dev/cu.usbserial-0 ← And this
```

---

## Build

### Install PlatformIO (first time only)

```bash
pip3 install platformio
pio --version
# PlatformIO X.X.X
```

### Build

```bash
cd handheld_crowpanel

# Build only
pio run -e crowpanel

# Build and flash
pio run -e crowpanel -t upload

# Open serial monitor (115200 baud)
pio device monitor -e crowpanel
```

### Build output

**First build** takes 10-15 minutes (downloads ESP-IDF components).  
**Subsequent builds** take ~2 minutes.

```
Building .../handheld_crowpanel/.pio/build/crowpanel/firmware.bin
...
Flash: 16 MB
PSRAM: 32 MB
==== [SUCCESS] Took 12.34 seconds ====
```

---

## Flash

```bash
# Automatic (uses upload_port from platformio.ini)
pio run -e crowpanel -t upload

# OR explicit port
pio run -e crowpanel -t upload -p /dev/cu.usbserial-0
```

**Expected output**:
```
Connecting....
Chip is ESP32-P4 Revision [X]
Writing at 0x00020000... (100%)
Hard resetting via RTS pin...
```

Device automatically reboots. You'll see boot logs in the monitor.

### If flash fails

**Problem**: "Failed to connect to ESP32"  
**Solution**: Hold **BOOT** button on CrowPanel while uploading, release when "Connecting..." appears

**Problem**: "Port already in use"  
**Solution**: Kill any existing monitor: `pkill -f "pio device monitor"`

---

## Monitor

```bash
# Automatic
pio device monitor -e crowpanel

# OR explicit port
pio device monitor -p /dev/cu.usbserial-0 -b 115200
```

**Expected startup logs**:
```
I (1234) display: RGB panel initialized: 800x480
I (5678) touch: GT911 touch initialized (800x480)
I (6789) main: Demo screen created
========== System Ready ==========
```

**Test display**: Look at CrowPanel screen
- White background
- "CrowPanel MVP" title (top)
- "Taps: 0" counter (center)
- "Tap Me!" button (gray box)

**Test touch**: Tap the button
- Counter increments: "Taps: 1", "Taps: 2", etc.
- Serial logs: `Counter: 1`, `Counter: 2`

---

## Project structure notes

- All pin definitions live in `main/board_config.h` — never hardcode GPIO numbers elsewhere.
- Display driver: `main/display/display.cpp` — RGB panel init + LVGL integration
- Touch driver: `main/touch/touch_gt911.cpp` — GT911 I2C driver + LVGL input
- Demo UI: `main/main.cpp` — Entry point + tap counter demo

---

## Troubleshooting

### Build fails: "esp_lcd.h not found"

**Cause**: PlatformIO didn't download ESP-IDF properly

**Fix**:
```bash
pio run -e crowpanel -t clean
rm -rf .pio/
pio run -e crowpanel
```

### Build fails: "board esp32-p4-* not found"

**Cause**: PlatformIO doesn't recognize ESP32-P4

**Fix**:
```bash
pip3 install --upgrade platformio
platformio platform update espressif32
pio run -e crowpanel
```

### Display shows garbage / no rendering

**Check**:
1. RGB pin assignments in `main/board_config.h` match CrowPanel silkscreen
2. GPIO values are correct (see CROWPANEL_FINDINGS.md)
3. Pixel clock speed in `main/display/display.cpp`

**Try lowering pixel clock**:
```cpp
// main/display/display.cpp, line ~25
.timings = {
    .pclk_hz = (20 * 1000 * 1000),  // Try 20 MHz instead of 25
    ...
}
```

### Touch not responding

**Check**:
1. I2C pins in `main/board_config.h`: SDA=45, SCL=46
2. GT911 address: 0x5D (default)
3. Serial logs show: "touch: GT911 touch initialized"

**Debug**:
```bash
# Enable I2C scan (advanced)
# See SETUP.md for details
```

### Application crashes/resets

**Check memory usage** (serial logs):
```
Free PSRAM: XXXX KB
Free IRAM: XXX KB
```

**Try reducing buffers**:
```cpp
// main/display/display.cpp
.buffer_size = (LCD_H_RES * LCD_V_RES) / 2,  // Half buffer
```

---

## Next steps (Phase 2+)

Once Phase 1 tests pass:
- [ ] Extract to `handheld_common/` (i18n, UI, BLE, RFID)
- [ ] Refactor original `handheld/` (SC01 Plus) to use shared modules
- [ ] Inspect hardware for RFID UART pins, buttons, backlight control
- [ ] Integrate RFID scanning
- [ ] Add settings menu

See [TASKS.md](TASKS.md) for full roadmap.

---

## Quick reference

```bash
# One-liner: build → flash → monitor
cd handheld_crowpanel && \
pio run -e crowpanel -t clean && \
pio run -e crowpanel && \
pio run -e crowpanel -t upload -p /dev/cu.usbserial-0 && \
pio device monitor -e crowpanel
```

---

## Files

| File | Purpose |
|---|---|
| `handheld_crowpanel/platformio.ini` | Build config, USB port, board definition |
| `handheld_crowpanel/sdkconfig.defaults` | ESP-IDF config (PSRAM, LVGL, flash size) |
| `handheld_crowpanel/main/board_config.h` | Pin definitions (edit when adding GPIO) |
| `handheld_crowpanel/main/display/display.cpp` | RGB LCD + LVGL driver |
| `handheld_crowpanel/main/touch/touch_gt911.cpp` | GT911 I2C driver |
| `handheld_crowpanel/main/CMakeLists.txt` | Component manifest |

---

## Support

- **Installation issues**: See `handheld_crowpanel/SETUP.md`
- **Test procedure**: See `handheld_crowpanel/TEST_PLAN.md`
- **Step-by-step checklist**: See `handheld_crowpanel/PHASE1_CHECKLIST.md`
- **Hardware analysis**: See `CROWPANEL_FINDINGS.md`
