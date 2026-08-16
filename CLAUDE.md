# Pilocows — Claude Code Project Guide

Animal traceability system for Pilo's farm. Tracks cattle via ISO 11784/85 RFID ear tags (FDX-B, 134.2 kHz, 11-digit EID).

## Repository Structure

```
pilocows/
├── handheld/           # ESP32-S3 firmware (SC01 Plus) — PlatformIO + ESP-IDF
├── handheld_crowpanel/ # ESP32-P4 firmware (CrowPanel) — PlatformIO + ESP-IDF [NEW]
├── handheld_common/    # Shared modules (planned Phase 2)
├── frontend/           # Tauri + React desktop app (macOS & Windows)
├── backend/            # Standalone Rust Axum REST API + SQLite
└── docs/               # Architecture, API spec, BLE protocol
```

**Rule**: backend/ is an independent binary. In Phase 1, Tauri launches it as a sidecar. Never couple it to Tauri internals — it must be deployable as a cloud service later with zero code changes.

**Multi-device strategy** (Phase 1+): handheld/ and handheld_crowpanel/ are separate implementations. Phase 2 will extract shared UI/driver logic to handheld_common/ so both variants use the same code where possible.

## Sub-project Overview

### handheld/ (SC01 Plus)
- **MCU**: ESP32-S3 (WT32-S3-WROVER-2-N8R2)
- **Board**: ZX3D50CE02S-USRC-4832 (SC01 Plus by Smart Panlee)
- **IDE**: VS Code + PlatformIO + ESP-IDF framework
- **UI library**: LVGL v8+ (via ESP-IDF component)
- **Display**: ST7796UI, 480×320, RGB565, 8-bit 8080 parallel interface
- **Touch**: FT6336U (resistive), I2C
- **RFID reader**: 134.2K AGV FDX-B ISO11784/85 TTL UART module

### handheld_crowpanel/ (CrowPanel Advanced 5")
- **MCU**: ESP32-P4 (dual-core RISC-V, 400 MHz) + ESP32-C6-MINI-1 (WiFi/BLE via SDIO)
- **Board**: CrowPanel Advanced 5" ESP32-P4 HMI AI Display (800×480 IPS)
- **IDE**: VS Code + PlatformIO + ESP-IDF framework
- **UI library**: LVGL v8.3 (via ESP-IDF component)
- **Display**: RGB parallel 16-bit, 800×480, IPS, 25 MHz pixel clock
- **Touch**: GT911 (capacitive, 5-point), I2C
- **Memory**: 16 MB Flash, 32 MB PSRAM
- **RFID reader**: 134.2K AGV FDX-B ISO11784/85 TTL UART module (UART1, RX=GPIO26, receive-only)
- **Status**: Phase 2 (SD storage, audio notes, BLE sync in progress)

### frontend/
- **Shell**: Tauri 2.x
- **UI**: React + TypeScript + Vite
- **Styling**: Tailwind CSS
- **i18n**: react-i18next (EN + ES)
- **Tauri Rust layer**: BLE central (connect to handheld), sidecar launcher for backend

### backend/
- **Language**: Rust
- **HTTP**: Axum
- **DB**: SQLite via sqlx (async, compile-time checked queries)
- **Migrations**: sqlx-cli
- **API**: REST, JSON

## Hardware Pin Reference

### SC01 Plus (ESP32-S3)

#### LCD (ST7796UI — 8080 8-bit parallel, NOT SPI)
| Signal   | GPIO |
|----------|------|
| BL_PWM   | 45   |
| LCD_RESET| 4    |
| LCD_RS   | 0    |
| LCD_WR   | 47   |
| LCD_TE   | 48   |
| LCD_DB0  | 9    |
| LCD_DB1  | 46   |
| LCD_DB2  | 3    |
| LCD_DB3  | 8    |
| LCD_DB4  | 18   |
| LCD_DB5  | 17   |
| LCD_DB6  | 16   |
| LCD_DB7  | 15   |

### Touch (FT6336U — I2C)
| Signal | GPIO |
|--------|------|
| TP_SDA | 6    |
| TP_SCL | 5    |
| TP_INT | 7    |
| TP_RST | 4    | ← shared with LCD_RESET

### Audio (I2S amplifier — onboard speaker connector)
| Signal | GPIO |
|--------|------|
| LRCK   | 35   |
| BCLK   | 36   |
| DOUT   | 37   |

### SD Card (SPI)
| Signal       | GPIO |
|--------------|------|
| SD_CS        | 41   |
| SD_DI (MOSI) | 40   |
| SD_CLK       | 39   |
| SD_DO (MISO) | 38   |

### RS485 (through transceiver chip)
| Signal | GPIO |
|--------|------|
| RXD    | 1    |
| RTS    | 2    |
| TXD    | 42   |

### Extended IO Header (our peripherals)
| Function       | GPIO | EXT Pin |
|----------------|------|---------|
| RFID UART RX   | 10   | EXT_IO1 |
| RFID UART TX   | 11   | EXT_IO2 |
| SCAN button    | 12   | EXT_IO3 |
| NAV button UP  | 13   | EXT_IO4 |
| NAV button DOWN| 14   | EXT_IO5 |
| Vibrator motor | 21   | EXT_IO6 |

Buzzer: use onboard I2S audio amplifier (tone generation via I2S).

### CrowPanel Advanced 5" (ESP32-P4)

#### LCD (RGB Parallel — 16-bit, 800×480)
| Signal     | GPIO |
|------------|------|
| PCLK       | 3    |
| DE         | 2    |
| HSYNC      | 40   |
| VSYNC      | 41   |
| DATA[0]    | 8    |
| DATA[1]    | 7    |
| DATA[2]    | 6    |
| DATA[3]    | 5    |
| DATA[4]    | 4    |
| DATA[5]    | 14   |
| DATA[6]    | 13   |
| DATA[7]    | 12   |
| DATA[8]    | 11   |
| DATA[9]    | 10   |
| DATA[10]   | 9    |
| DATA[11]   | 19   |
| DATA[12]   | 18   |
| DATA[13]   | 17   |
| DATA[14]   | 16   |
| DATA[15]   | 15   |

Pixel clock: 25 MHz. Refresh ~42 Hz @ 800×480 16-bit RGB565.

#### Touch (GT911 — I2C, capacitive)
| Signal | GPIO |
|--------|------|
| SDA    | 45   |
| SCL    | 46   |
| INT    | 42   |
| RST    | 36   |

#### Power Management (LDO)
| Rail | Voltage | Purpose |
|------|---------|---------|
| LDO3 | 2.5V    | Camera, misc (not used in Phase 1) |
| LDO4 | 3.3V    | I/O rail (touch, display control) |

#### Backlight & Audio
- **Backlight**: Controlled by STC8H1KXX management MCU (`peripheral/bsp_stc8h1kxx`, I2C)
- **Audio**: I2S speaker + mic implemented, see pin tables below
- **Camera**: MIPI CSI (not used for pilocows)

#### Connectivity Module — ESP32-C6-MINI-1 (WiFi/BLE via SDIO, through ESP-HOSTED)
| Signal            | GPIO |
|-------------------|------|
| SDIO CMD          | 54   |
| SDIO CLK          | 53   |
| SDIO D0           | 52   |
| SDIO D1           | 51   |
| SDIO D2           | 50   |
| SDIO D3           | 49   |
| C6 RESET          | 20   |

The C6 handles WiFi/BLE radio; NimBLE on the P4 talks to it over this SDIO
link via ESP-HOSTED. **GPIO 20 and 49-54 are reserved for this link — never
reassign them.** Toggling/resetting GPIO 20 resets the C6 module outright.

#### SD Card (SDMMC, 1-bit mode, `peripheral/bsp_sd`)
| Signal | GPIO |
|--------|------|
| CLK    | 43   |
| CMD    | 44   |
| D0     | 39   |

ESP32-P4 SDMMC slot 0's D0-D3 are dedicated (non-GPIO-matrix) pins — the
driver rejects `GPIO_NUM_NC` for the unused D1-D3, so `SDMMC_SLOT_CONFIG_DEFAULT()`
silently pre-fills them with this board's defaults (GPIO 40/41/42 — display
HSYNC/VSYNC + touch INT). A failed mount's cleanup path resets those three
pins to floating inputs, so `sd_init()` must run before display/I2C/touch
are ever brought up (see `main.c` `app_main()` ordering and `bsp_sd.c`).

#### Speaker (I2S, `peripheral/bsp_audio`)
| Signal | GPIO |
|--------|------|
| LRCLK  | 21   |
| BCLK   | 22   |
| SDATA  | 23   |

#### Microphone (I2S, `peripheral/bsp_mic`)
| Signal | GPIO |
|--------|------|
| CLK    | 24   |
| DIN    | 25   |

#### RFID Reader (UART1, receive-only)
| Signal | GPIO |
|--------|------|
| RX     | 26   |

#### Buttons (`button_driver.c`)
| Function | GPIO |
|----------|------|
| UP       | 48   |
| DOWN     | 31   |
| SELECT   | 32   |

#### Vibrator (LEDC/PWM, `feedback_driver.c`)
| Function | GPIO |
|----------|------|
| Vibrator | 30   |

No buzzer on this board — scan confirmation sounds play through the
onboard speaker (`bsp_audio`) instead. GPIO 47 (formerly buzzer) is free.

#### RTC (DS3231, `peripheral/bsp_rtc`)
I2C address `0x68`. Shares the existing I2C bus (SDA=GPIO45, SCL=GPIO46 —
same bus as touch and STC8H1KXX) via `bsp_i2c`'s `i2c_dev_register()`; no
dedicated pins. Wire the module to the board's external I2C header. Ported
from the SC01 Plus handheld's driver (`handheld/src/rtc/rtc.cpp`), adapted
from that board's own dedicated I2C bus to this board's shared one.
`main/soft_rtc.c` prefers this hardware RTC when present, falling back to
an NVS-persisted last-known time otherwise.

## System Architecture

```
┌─────────────────┐        BLE (GATT)        ┌──────────────────────────┐
│   Handheld      │ ◄───────────────────────► │   Frontend (Tauri+React) │
│   ESP32-S3      │    scan list + events     │   macOS / Windows        │
│                 │                           │                          │
│  - RFID scan    │                           │  - BLE central           │
│  - Local store  │                           │  - Animal records UI     │
│  - LVGL UI      │                           │  - EN/ES i18n            │
└─────────────────┘                           │                          │
                                              │   Tauri sidecar starts ──┼──►  ┌─────────────┐
                                              │   backend on startup     │     │  Backend    │
                                              └──────────────────────────┘     │  Rust+Axum  │
                                                        │  HTTP REST            │  SQLite     │
                                                        └──────────────────────►└─────────────┘
```

## Development Phases

### Phase 1 — Handheld MVP
1. PlatformIO project scaffold + board config (SC01 Plus pinout)
2. LCD driver via `esp_lcd` + `esp_lcd_panel_io_i80` → ST7796UI
3. Touch driver via I2C → FT6336U
4. LVGL integration + basic screen framework
5. RFID UART driver (UART1, GPIO 10/11)
6. Scan mode: read EID → display → store in NVS/SPIFFS
7. Buzzer (I2S tone) + vibrator (GPIO 21) alerts
8. Settings menu (language EN/ES, buzzer, vibrator)
9. BLE GATT server — expose scan list for desktop download

### Phase 2 — Backend
1. Rust project scaffold (Axum + sqlx + SQLite)
2. Database schema + migrations (tags, animals, health records)
3. REST API endpoints (see docs/api-spec.md)

### Phase 3 — Frontend
1. Tauri + React + TypeScript scaffold
2. BLE central — connect to handheld, pull scan list
3. Tag inventory management
4. Animal registration (tag assignment, breed, category, DOB)
5. Health records: vaccination, pregnancy, TB test, weighing, removal
6. EN/ES i18n via react-i18next

### Phase 4 — Integration & Polish
1. End-to-end BLE → frontend → backend flow
2. Reports / filters
3. Auth hooks (structure only, no implementation)
4. Backend portability test (run as standalone server)

## Agent Usage Recommendations

Use separate Claude Code agents (subagents) for each sub-project to avoid context pollution:

| Task | Recommended approach |
|------|---------------------|
| Handheld C++ / ESP-IDF | Dedicated session in `handheld/` — use PlatformIO CLI |
| Backend Rust API | Dedicated session in `backend/` |
| Frontend React UI | Dedicated session in `frontend/src/` |
| Tauri Rust shell | Work in `frontend/src-tauri/` alongside frontend session |
| Cross-cutting design | Main session with full repo context |

**Useful Claude skills to use:**
- `/simplify` — after writing a module, run this to catch over-engineering
- `/commit` — for structured commit messages following project conventions

## Coding Conventions

### C++ (handheld)
- ESP-IDF coding style (snake_case, `TAG` for log tags)
- One `.cpp`/`.h` pair per module, placed in its named subdirectory
- Pin definitions centralized in `include/board_config.h`

#### i18n rules (handheld UI) — strictly enforced
- **Every** user-visible string must go through `i18n_t(STR_...)`. Never hardcode visible text, not even in modal popups or helper functions.
- Adding a new string requires changes to **three** files — miss any one and the string stays English:
  1. `strings_en.h` — add `#define STR_FOO "English text"`
  2. `strings_es.h` — add `#define STR_FOO "Texto en español"`
  3. `i18n.cpp` — add `{ "English text", "Texto en español" }` to `s_table[]`
- Every `lv_label_set_text` target that contains a translatable string must have its `lv_obj_t*` stored as a file-level static so it can be refreshed in that screen's `refresh_language()` function.
- `refresh_language()` must cover **all** labels on the screen, including those inside dynamically created widgets (buttons, dropdowns, etc.).

### Rust (backend + Tauri)
- `snake_case` everywhere
- Errors via `thiserror` crate, no `unwrap()` in production paths
- All DB queries in `db/` layer — routes must not contain raw SQL
- API routes follow REST conventions: `GET /animals`, `POST /animals`, etc.

### React / TypeScript
- Functional components only, hooks for state
- All visible strings via `useTranslation()` hook — never hardcode
- API calls centralized in `src/api/` — components must not fetch directly

## BLE Protocol Summary
(Full spec in docs/ble-protocol.md)

- Handheld acts as **GATT server**
- Desktop acts as **GATT central**
- Service UUID: `PILOCOWS_SCAN_SERVICE`
- Characteristic: `SCAN_LIST` — read/notify, returns JSON array of `{eid, timestamp, event_type, notes}`
- Characteristic: `DEVICE_STATUS` — read, returns `{battery, scan_count, firmware_version}`
- Characteristic: `CONTROL` — write, accepts commands (`clear_list`, `set_time`)

## REST API Summary
(Full spec in docs/api-spec.md)

Base URL (local): `http://127.0.0.1:8742`

Key resource groups:
- `/tags` — purchased tag inventory
- `/animals` — animal registration and profiles
- `/animals/{id}/vaccinations`
- `/animals/{id}/pregnancies`
- `/animals/{id}/tb-tests`
- `/animals/{id}/weights`
- `/animals/{id}/removal`
- `/sync/scans` — ingest BLE scan batch from frontend

## i18n

Both handheld and frontend support **English (en)** and **Spanish (es)**.

- Handheld: compile-time string tables in `i18n/strings_en.h` / `i18n/strings_es.h`, selected via NVS setting
- Frontend: runtime switching via react-i18next, locale files in `src/i18n/en.json` / `src/i18n/es.json`
