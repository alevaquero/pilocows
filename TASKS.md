# Pilocows Handheld CrowPanel — Feature Implementation Roadmap

**Status**: Phase 1 MVP (Hardware drivers + demo UI)  
**Last Updated**: 2026-07-24

---

## Phase 1: Core Hardware Drivers (IN PROGRESS)

### Display & Touch
- [x] RGB parallel LCD driver (ESP32-P4 → 800×480 IPS)
- [x] GT911 capacitive touch via I2C
- [x] LVGL 8.3 integration with RGB framebuffer
- [x] Minimal demo UI (tap-to-increment counter)
- [x] Basic button framework (placeholder for future buttons)

### Known Limitations (Phase 1)
- [ ] Backlight brightness control (STC8-managed, not direct P4 GPIO)
- [ ] No GPIO buttons defined yet (need hardware inspection)
- [ ] No audio/buzzer support
- [ ] No vibrator support
- [ ] RTC not yet integrated
- [ ] RFID reader not yet integrated

### Success Criteria Met
- ✅ Device boots, LVGL renders to RGB display
- ✅ Touch events detected and functional
- ✅ At least one interactive demo screen
- ✅ No build errors or critical warnings
- ✅ Task list created for Phase 2+

---

## Phase 2: Shared Code Extraction & Module Integration (PLANNED)

### Priority 1: Extract Shareable Modules
- [ ] `handheld_common/i18n/` — Extract English/Spanish string tables
- [ ] `handheld_common/fonts/` — Copy font assets, scale for 800×480
- [ ] `handheld_common/ble/` — Extract BLE GATT server logic (MCU-agnostic)
- [ ] `handheld_common/rfid/` — Extract RFID reader driver (UART-agnostic)

### Priority 2: Display Abstraction Layer
- [ ] Create `handheld_common/ui/lv_display_if.h` — Abstract display interface
- [ ] Implement display abstraction in both SC01 Plus & CrowPanel
- [ ] Move UI screens to shared folder (with display callbacks)
- [ ] Verify UI renders identically on both devices

### Priority 3: Storage & Data
- [ ] `handheld_common/storage/` — Session storage (NVS/SPIFFS agnostic)
- [ ] `handheld_common/rtc/` — RTC logic (I2C-agnostic)
- [ ] Verify both boards can read/write session data

### Refactor Existing Handheld
- [ ] Update `handheld/src/` to use `handheld_common/` modules
- [ ] Remove duplicated code from handheld/src/
- [ ] Test on SC01 Plus to ensure backward compat
- [ ] Commit as "refactor: use shared handheld modules" PR

---

## Phase 3: Full Feature Parity (PLANNED)

### RFID Scanning
- [ ] Identify available UART pins on CrowPanel (GPIO 10/11 or alternate?)
- [ ] Port RFID reader driver from SC01 Plus
- [ ] Test EID reads on CrowPanel hardware
- [ ] Add to `handheld_crowpanel/src/rfid/`

### Session Management
- [ ] Integrate storage layer (from Phase 2)
- [ ] Implement local scan session storage (SPIFFS)
- [ ] Test write/read performance on CrowPanel PSRAM

### BLE Sync
- [ ] Integrate BLE GATT server (from Phase 2)
- [ ] Implement scan list exposure over BLE characteristic
- [ ] Test BLE central connection (desktop app)
- [ ] Verify data sync from device to frontend

### Backlight Control
- [ ] Research STC8 PWM register interface
- [ ] Implement backlight command via I2C/UART to STC8
- [ ] Test brightness levels (20%, 50%, 100%)
- [ ] Optional: add to settings menu

### Buzzer & Vibrator
- [ ] Confirm GPIO availability for buzzer (on CrowPanel header?)
- [ ] Implement GPIO-based buzzer driver
- [ ] Confirm GPIO availability for vibrator motor
- [ ] Test alert sequences (scan, error, success)

### RTC Integration
- [ ] Confirm DS3231 I2C address on CrowPanel
- [ ] Port RTC driver from SC01 Plus
- [ ] Test time setting via BLE command
- [ ] Verify RTC persists across power cycles

### Settings Menu
- [ ] Add language selection (EN/ES)
- [ ] Add brightness control
- [ ] Add buzzer/vibrator toggles
- [ ] Add firmware version display
- [ ] Save to NVS

### OTA Updates
- [ ] Implement HTTP server for firmware download
- [ ] Test OTA via BLE + WiFi (Phase 4, requires C6)
- [ ] Verify rollback on bad firmware

---

## Phase 4: Advanced Features (FUTURE)

### WiFi/BLE via C6-MINI-1
- [ ] Enable ESP_HOSTED framework (SDIO to C6)
- [ ] Implement WiFi provisioning
- [ ] Expose BLE to C6 for extended range
- [ ] Test mobile app connectivity via both

### Logging & Telemetry
- [ ] Implement local log storage (SPIFFS)
- [ ] Send logs via BLE to desktop app
- [ ] Add analytics (scan rate, uptime, errors)

### Security
- [ ] Add auth token support (optional, defer if not critical)
- [ ] Implement secure BLE pairing
- [ ] Encrypt local storage (if sensitive data grows)

### Performance Tuning
- [ ] Profile LVGL rendering (frame rate, CPU usage)
- [ ] Optimize PSRAM buffer allocation
- [ ] Test under high RFID scan load

---

## Hardware TBD (Requires Inspection)

| Item | Status | Notes |
|------|--------|-------|
| Button GPIOs | ❓ TBD | CrowPanel examples don't show user buttons; may need to repurpose reset/boot buttons or GPIO headers |
| Backlight PWM | ❓ TBD | Controlled by STC8, not direct P4 GPIO; need I2C/UART command interface |
| Buzzer GPIO | ❓ TBD | Not exposed in examples; may be via I2S audio amplifier or GPIO header |
| Vibrator GPIO | ❓ TBD | Not in examples; may not be wired on CrowPanel (check PCB) |
| RTC I2C | ❓ TBD | DS3231 not in factory examples; likely available via I2C but needs address confirmation |
| RFID UART | ❓ TBD | CrowPanel examples only show UART3 (power); UART1 may be available on GPIO headers |
| WiFi/BLE via C6 | ❓ TBD | SDIO interface present; full integration requires C6 firmware + ESP_HOSTED driver setup |

---

## Git Workflow

### Phase 1 (Now)
```bash
git add handheld_crowpanel/
git commit -m "add: crowpanel esp32-p4 handheld phase 1 mvp

- RGB 800×480 display driver (16-bit parallel)
- GT911 capacitive touch via I2C
- LVGL 8.3 integration with PSRAM framebuffer
- Demo UI: tap-to-increment counter
- platformio + ESP-IDF build system
- Task list for Phase 2+ features"
```

### Phase 2 (After hardware TBD)
```bash
git checkout -b refactor/handheld-shared-modules
# Extract to handheld_common/
# Update both handheld/ and handheld_crowpanel/ to use shared code
git commit -m "refactor: extract handheld shared modules

- handheld_common/{i18n,fonts,ble,rfid,ui,storage,rtc}
- Display abstraction layer for dual-device UI
- Both SC01 Plus & CrowPanel use shared logic"
```

---

## Testing Strategy

### Phase 1 (MVP)
- Build on CrowPanel hardware
- Verify display renders correctly
- Verify touch input detected
- Verify no memory leaks or crashes (monitor logs)
- No tests required (UI is manual verification)

### Phase 2 (Shared Code)
- Regression test on SC01 Plus (original handheld)
- Verify UI layout scales to 480×320 (may need tweaks)
- Verify shared modules work on both MCUs
- BLE protocol tests (desktop app connects)

### Phase 3 (Features)
- RFID scanning tests (read EID sequences)
- Session storage tests (write/read 100+ scans)
- BLE sync tests (desktop pulls scans)
- Settings persistence (power cycle test)

### Phase 4 (Advanced)
- WiFi provisioning test
- OTA update test (dummy firmware)
- Extended BLE range test (C6 via SDIO)
- Load test (high RFID scan rate, memory usage)

---

## Blockers & Dependencies

- **RFID UART pins**: Must identify available UART on CrowPanel before Phase 3
- **Backlight control**: Requires STC8 register documentation
- **Button GPIO**: Hardware inspection needed
- **RTC I2C address**: Datasheet or multi-probe needed
- **C6 firmware**: WiFi/BLE phase requires ESP32-C6 toolchain setup (Phase 4)

---

## Success Metrics

- [x] Phase 1: Display + touch working, demo UI interactive
- [ ] Phase 2: Both handheld& and handheld_crowpanel build from shared source
- [ ] Phase 3: Full feature parity with SC01 Plus implementation
- [ ] Phase 4: Mobile app can sync with both device variants
