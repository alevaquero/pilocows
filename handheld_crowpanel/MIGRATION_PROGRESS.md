# Handheld → CrowPanel Migration Progress

### Unified text entry across the app (2026-07-26)
Every place the on-screen keyboard is used (animal note in `screen_scan.c`, note edit in
`screen_session_list.c`, name/note fields in `screen_session_new.c`, vaccine/test name
entry, wifi password) now goes through one shared modal, `ui_text_entry.c`/`.h`:
label on top, input field below it, a Cancel (red, left) / OK (default accent, right) row
right above the keyboard. Editing always happens in the modal's own temporary textarea —
the screen's real field/data is only touched from the `on_confirm` callback, so Cancel is
always a true revert with no special-case snapshot/restore logic needed per screen.
Password fields (wifi) pass `password = true` for masking + an eye-toggle button built
into the modal itself.

This replaced several different, inconsistent patterns: `screen_scan.c` and
`screen_session_list.c` previously had modals whose Cancel/OK both saved unconditionally;
`screen_session_new.c` used a hide/show/reposition dance (`show_keyboard()`/
`restore_layout()`) to repurpose one on-screen keyboard between two live-edited fields;
`screen_wifi.c` toggled between two full-screen layouts on focus/defocus
(`enter_keyboard_mode()`/`exit_keyboard_mode()`); vaccine/test settings already had correct
confirm-vs-cancel semantics but no visible buttons for it. All of that per-screen plumbing
is now gone — each screen just calls `ui_text_entry_show()` with a label, initial text, and
an `on_confirm` callback.

Also fixed two real bugs found while building the custom on-screen keyboard layout
(`ui_keyboard.c`, iOS-inspired QWERTY): (1) the "123" key was typing the literal text "123"
because LVGL's built-in handler only recognizes "1#" for that switch — fixed by having our
own event callback intercept "123"/"ABC"/the shift icon before falling through to LVGL's
default handler; (2) the space bar was invisible and non-interactive because its width
value (16) overflowed LVGL's 4-bit width field (`LV_BTNMATRIX_WIDTH_MASK = 0x000F`) into
bit `0x10`, which is `LV_BTNMATRIX_CTRL_HIDDEN` — all button widths must stay 1-15. Shift
also now behaves like iOS: one tap capitalizes only the next key press, then auto-reverts
to lowercase, instead of behaving as a caps-lock toggle.

Verified: `idf.py build` passes clean (0 errors) after every file's edits, built
incrementally (with `idf.py reconfigure` whenever a new source file was added, since
CMake's `GLOB_RECURSE` doesn't pick up new files without it). **Not yet tested on
hardware** — needs a flash + walkthrough of every text-entry point (animal note, session
note, session name/note in "New Session", vaccine/test name add, wifi password) to confirm
Cancel truly discards, OK truly commits, and the keyboard behaves as expected on the real
touchscreen.

### Display rotated to portrait (2026-07-25)
New hardware revision is physically bigger and mounted in portrait, so the whole UI moved
from 800x480 landscape to 480x800 portrait. Two parts:

**Driver (bsp_display.h/.c)**: the RGB parallel panel's physical scan timing is fixed at
800(h)x480(v) — that can't change regardless of mounting — but the ESP-IDF RGB LCD driver
(verified directly in `esp_lcd_panel_rgb.c` / `rgb_lcd_rotation_sw.h`, this IDF checkout)
implements real `swap_xy`/`mirror_x`/`mirror_y` pixel remapping during flush, so LVGL and
touch can run in a different logical resolution than the physical one. Added a
`DISPLAY_ROTATE_90` flag in `bsp_display.h` plus `LV_H_RES`/`LV_V_RES` macros (480/800,
swapped from the physical `H_size`/`V_size`); `touch_init()`'s `tp_cfg` and `lvgl_init()`'s
`disp_cfg` both now use `LV_H_RES`/`LV_V_RES` and `{ .swap_xy = true, .mirror_x = false,
.mirror_y = true }` (matches `esp_lvgl_port`'s own `LV_DISP_ROT_90` convention). This
mirror/swap combination is a best guess — **unverified on hardware**, since the actual
mounting orientation can't be confirmed without the physical unit. If the image comes up
upside-down or mirrored, flip `mirror_y` <-> `mirror_x` in both places (one-line fix, see
comments at both call sites).

**UI (all of `main/`)**: every screen used hardcoded absolute-pixel layouts designed for
800x480 landscape (confirmed zero flex/grid/`LV_PCT` usage anywhere), so this was a full
per-screen coordinate redesign, not just a config change. Design language: headers resized
to 480 width (same structure otherwise); "label-left/field-right" rows became
"label-above/field-below" (`screen_settings.c`'s 8 rows, `screen_session_new.c`'s
type/name/note rows, `screen_wifi.c`'s network/password rows); multi-column clusters wider
than 480px were restructured — `screen_scan.c`'s weighing 7-button row became 2 rows of 3,
its pregnancy 3x2 grid became 2x3, its test 3-across row became a stacked column;
`screen_settings.c`'s date/time modal 5-column spinbox became 2 rows (Y/M/D, then H/M);
`screen_session_list.c`'s row (name+count+3 icons crammed into 800px) became a 2-line row;
`screen_session_menu.c`'s New+List button row now stacks vertically. Popups/modals
(`ui_popup.c` and all delete-confirm/add overlays) shrank to fit within 480 width, using
`lv_obj_center()` instead of hardcoded X/Y where practical. `screen_test_settings.c` and
`screen_vaccine_settings.c` (near-duplicate files) got the identical set of edits applied
independently — not merged into a shared component, out of scope for this task.
`ui_manager.c` and `main.c` needed no changes (no dimension logic in either).

Files touched: `bsp_display.h`, `bsp_display.c`, `ui_popup.c`, `screen_scan.c`,
`screen_settings.c`, `screen_session_list.c`, `screen_session_menu.c`,
`screen_session_new.c`, `screen_wifi.c`, `screen_test_settings.c`,
`screen_vaccine_settings.c`, `screen_ble_sync.c`.

Verified: `idf.py build` passes clean (0 errors) after every file's edits, built
incrementally. **Not yet tested on hardware** — needs a flash + visual walkthrough of every
screen (scan, settings, session menu/list/new, wifi, test/vaccine settings, BLE sync modal)
to confirm orientation is right-side-up, touch points land correctly, and nothing overlaps
or clips at the new proportions.

## STATUS: All planned modules ported, full `idf.py build` passes clean (0 errors).
On-device tested by the user (2026-07-25): display + touch + UI work. C6 already has
compatible ESP-HOSTED slave firmware flashed (confirmed via boot log: "Identified slave
[esp32c6]", HCI over SDIO, BLE only) — that physical step was already done, contrary to
this doc's earlier assumption. One crash found and fixed on real hardware (see below).

### Crash found on hardware + fix (2026-07-25)
Boot log showed WiFi/ESP-HOSTED transport come up correctly, RFID driver start fine, then
`ble_gatt_server_init()` -> `nimble_port_init()` aborted: `ESP_ERROR_CHECK failed... at
hal_uart_config`, called from NimBLE's UART HCI transport (`hci_uart_configure`).
Root cause: `BT_NIMBLE_TRANSPORT_UART` defaults to `y` in ESP-IDF whenever the target has
no native BT controller (true for P4) — I had wired `CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE`/
`CONFIG_ESP_HOSTED_NIMBLE_HCI_VHCI` but never explicitly turned this UART default off, so
NimBLE was trying to `uart_driver_install()` on UART1 for its own HCI — the exact same
UART the RFID driver already owns. Fixed: added `# CONFIG_BT_NIMBLE_TRANSPORT_UART is not
set` to sdkconfig.defaults. Verified: `ESP_HOSTED_ENABLE_BT_NIMBLE=y` and
`ESP_HOSTED_NIMBLE_HCI_VHCI=y` now actually resolve in the generated sdkconfig (they didn't
before — VHCI mode was silently unreachable since its Kconfig `depends on` clause requires
UART transport to be off). Rebuilt clean. **Not yet re-tested on hardware** — user should
reflash and confirm this specific crash is gone.
Also noted (non-fatal, flag for later): boot log shows `Version mismatch: Host [2.7.0] >
Co-proc [2.3.0]` for esp_hosted — could cause RPC timeouts down the line; the C6's hosted
slave firmware may need updating to match host version 2.7.0 eventually.

Full port of `handheld/` (ESP32-S3, SC01 Plus, 480x320) functionality into
`handheld_crowpanel/` (ESP32-P4, CrowPanel Advanced 5", 800x480). Scale
factor for UI: 800/480 = 1.667x horizontal, 480/320 = 1.5x vertical
(use 1.5x uniformly for consistent proportions unless noted).

Build system note: handheld_crowpanel uses plain ESP-IDF (`idf.py build`/`idf.py flash`),
NOT PlatformIO — PIO doesn't fully support ESP32-P4 yet.

Decisions locked in with user (2026-07-25):
- **RTC**: CrowPanel has no DS3231/RTC chip. Software-only clock (NVS-persisted
  best-effort + BLE/settings set_time). No I2C RTC driver ported.
- **BLE/WiFi**: User opted to attempt full ESP-HOSTED bring-up (ESP32-C6 co-processor
  over SDIO) rather than leaving stubbed. This is real hardware bring-up that
  cannot be fully verified without on-device testing — flagged for user validation.

## Status legend
- [ ] not started
- [~] in progress
- [x] done (compiles / logically complete, not yet hardware-tested)

## Modules

- [x] i18n system (strings_en.h / strings_es.h / i18n.c+h) — foundation, everything else depends on it
- [x] ui_popup.c — generic modal/confirm dialog
- [x] session_storage.c — expand to full per-tag CRUD (now SPIFFS-backed, was NVS-only). Added a
      2MB "storage" SPIFFS partition (partitions.csv) since NVS (16KB) can't hold real tag volume.
      Also added vaccine_cfg_t/test_cfg_t config storage (needed by tasks 12/13).
- [x] rfid_driver.c — finished UART wiring AND replaced the wrong protocol: the crowpanel stub
      assumed raw 11-digit-ASCII-between-STX/ETX framing, but the real (tested, working) SC01
      driver uses the WL-134 30-byte hex-ASCII/checksum frame producing a 15-digit EID
      (3-digit country + 12-digit animal id). Ported the real protocol. Bumped
      SESSION_EID_MAX 11->15 in session_storage.h to match.
- [x] feedback_driver.c — added buzzer_success/buzzer_duplicate/vibrator_success/vibrator_duplicate
      named patterns (matching original's beep/pulse timing) plus enabled/disabled toggles, on top
      of the existing generic buzzer_beep/vibrator_pulse primitives that were already there.
- [x] main.c — full app flow ported (RFID queue -> auto-save-previous -> duplicate-check ->
      show tag -> feedback -> BLE notify). Added soft_rtc.c/h as the RTC replacement
      (task 16, see below) and wired buzzer/vibrator enabled-state from saved NVS settings
      at boot. Uses lvgl_port_lock/unlock (esp_lvgl_port's real API) instead of the
      non-existent display_lvgl_lock() the original called.
- [x] screen_session_menu.c — replaced with a faithful port of the original (which has
      NO header bar and NO close-session button — the previous crowpanel version had
      invented both, plus never used i18n at all). Removed the invented `screen_session_menu_refresh()`
      export (nothing outside the file called it; original uses an internal static + screen-loaded event).
- [x] screen_session_new.c — full port. The old version had a hardcoded `return;` after a
      debug label at the top of screen_session_new_create() that disabled almost the entire
      screen (type buttons, name field, create/cancel) — dead code, never noticed because
      the screen still "loaded" (just showed one label). Replaced entirely: dropdown type
      selector (was a 2x3 button grid with no i18n), name+note textareas w/ on-screen
      keyboard, vaccine checkboxes, test dropdown, matching the original 1:1.
      Extended session_storage's session_create() signature to take vax_ids/vax_count/test_id
      (previously only name+type) and exported session_build_default_name() (previously
      static) since the type dropdown's on-change handler needs it directly.
- [x] screen_session_list.c — full port: per-row activate/edit-note/delete icon buttons,
      delete-confirmation card, session note edit modal+keyboard. Converted the original's
      C++ lambda event callbacks to named static functions (C has no lambdas/closures).
      Old crowpanel version only had tap-to-activate (no icons, no note editing, no delete).
- [x] screen_scan.c — full port done (was 234 lines/stub, now full port of the 1150-line
      original: header w/ clock+count+note button, type-specific panels for
      weighing/vaccination/pregnancy/test/general/removal, animal-note modal+keyboard,
      flash overlay, no-session overlay). Scaled 5/3x horiz, 3/2y vert from 480x320->800x480.
      Enabled CONFIG_LV_FONT_MONTSERRAT_36=y in sdkconfig for the large EID readout.
      Adapted from original's packed tag_record_t.data[16] byte payload to crowpanel's
      named-field tag_record_t (weight_kg/pregnancy/test_result/vaccines/removal_reason).
      Fixed a latent enum-order bug: original indexed test buttons by raw int matching
      ITS OWN test_result_t value order; crowpanel's enum has a different value order,
      so button->value mapping now uses explicit symbolic array (s_test_btn_val[]) instead
      of casting the loop index, same pattern already used for pregnancy buttons.
- [x] screen_settings.c — full port: language switch, buzzer/vibrator toggles (now
      persisted to NVS via nvs_storage.h - infra was already there, just unused before,
      "TODO: Save to NVS" comments in the old stub), brightness slider (uses bsp_display's
      set_lcd_blight, not the original's own display_set_brightness which doesn't exist here),
      date/time spinbox modal (uses soft_rtc_set_time instead of rtc_set_time+settimeofday),
      links to WiFi/Vaccines/Tests/Sync. Simplified the language-change fan-out: original
      manually calls refresh_language() on 7 named screens (and misses screen_test_settings —
      a bug in the original); crowpanel's ui_manager already centralizes this via
      ui_manager_refresh_language(), so on_language() just calls that instead, fixing the
      missed screen for free. Added app_version.h for a single shared FIRMWARE_VERSION string
      (main.c and this file both display it; previously only main.c had it, hardcoded separately).
- [x] screen_vaccine_settings.c — full port: list w/ per-row delete, add overlay w/
      keyboard, delete-confirm overlay. **Found a pre-existing linker-breaking bug**:
      screen_stubs.c ALSO defined screen_session_new_create/load/refresh_language,
      screen_session_list_*, screen_scan_*, and screen_settings_* — duplicate symbols
      against the real screen_session_new.c/screen_session_list.c/screen_scan.c/
      screen_settings.c files that already existed side by side. This project could not
      have linked successfully before this migration touched it. Stripped screen_stubs.c
      down to only the screens not yet ported (wifi, test_settings) — will delete it
      entirely once those two are done.
- [x] screen_test_settings.c — full port, exact structural mirror of vaccine settings with
      test terminology (confirmed by diffing originals). screen_stubs.c now only contains
      the wifi stub (task 14 will remove it entirely once that's ported).
- [x] screen_wifi.c — full port: scan dropdown, password field w/ show/hide + full-screen
      keyboard mode, connect button w/ timeout, auth-error banner, live status/IP line.
      Created wifi_manager.h/c with the same API shape as the original (stub body — real
      impl needs ESP-HOSTED, same as BLE). screen_stubs.c is now fully empty of purpose and
      has been deleted; every screen has a real implementation.
- [x] screen_ble_sync.c — created (was missing entirely). Turns out it's a modal overlay
      shown on demand via screen_ble_sync_show_modal(), NOT a distinct ui_manager screen —
      no SCREEN_BLE_SYNC enum entry needed, contrary to my initial assumption when scoping
      this task. Redesigned ble_gatt_server.h to the full API shape the original ble_server.h
      has (start/stop_advertising, status callback, is_connected, etc.) instead of the old
      minimal ScanList/DeviceStatus-only stub — body is still a stub (real impl is tasks 17-18),
      but the surface now matches what the UI needs. Removed the now-dead ScanList/DeviceStatus/
      ScanRecord types and nvs_add_scan/load_scans/save_scans/clear_scans from nvs_storage.h/c
      (superseded by session_storage's per-session tag persistence).
- [x] software time-keeping (no RTC) — added soft_rtc.h/c: soft_rtc_init() restores
      last-known time from NVS at boot (best effort), soft_rtc_set_time() does
      settimeofday()+persists. Replaces original's DS3231 driver 1:1 at call sites.
- [x] ESP-HOSTED enablement — found that Elecrow's own board examples
      (example/V1.0/idf-code/Lesson16/Lesson17 in the factory sourcecode repo) already have
      a WORKING, board-validated ESP-HOSTED+SDIO config for this exact P4+C6 combo. Also
      found that `sdkconfig.defaults` already had most of the SDIO pin config from a prior
      session (CMD=54,CLK=53,D0=52,D1=51,D2=50,D3=49, C6 reset=GPIO20) but idf_component.yml
      still had the esp_hosted/esp_wifi_remote deps commented out, so those Kconfig options
      didn't exist yet (hence "unknown kconfig symbol" warnings). Fixed: uncommented deps
      (esp_hosted ~2.7.0, esp_wifi_remote ^0.16.1, both gated to esp32p4/esp32h2 targets per
      Elecrow's manifest), added 2 missing config lines (CONFIG_SLAVE_IDF_TARGET_ESP32C6,
      CONFIG_ESP_HOSTED_P4_DEV_BOARD_NONE), added esp_wifi/bt/esp_netif to CMakeLists REQUIRES.
      `idf.py set-target esp32p4` now configures cleanly with "-- Using Hosted Wi-Fi" and pulls
      in esp_hosted + esp_wifi_remote + eppp_link + esp_serial_slave_link. NOTE: the board's
      example uses Bluedroid (CONFIG_BT_BLUEDROID_ENABLED=y, already set), not NimBLE — ble_gatt_server.c
      should target Bluedroid APIs to match this validated config.
      REMAINING PHYSICAL STEP (cannot be done from code): the ESP32-C6 co-processor itself
      must be flashed with the matching ESP-HOSTED slave firmware for any of this to work on
      real hardware — that's a separate flash target the esp_hosted component provides, not
      something this migration can do or verify without the board in hand.
- [x] ble_gatt_server.c — real NimBLE GATT server ported from the original ble_server.cpp
      (which turned out to already use NimBLE, not Bluedroid — the Elecrow board example's
      sdkconfig defaulted to Bluedroid, but ESP-HOSTED's Kconfig also has a NimBLE path
      (CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE) so switched to that instead of reimplementing
      the whole GATT server against a different API). Protocol: 5 characteristics
      (SESSION_LIST/DEVICE_STATUS/CONTROL/SESSION_DATA/SESSION_META) matching docs/ble-protocol.md's
      richer session-sync design (original's ble-protocol.md notes in CLAUDE.md were a
      simplified summary — the real implementation syncs full session+tag data, not just a
      flat scan list). Extended session_storage with session_mark_synced() and
      session_list_records_paged() (paginated, works on ANY session by ID, not just active) —
      needed for BLE to page through historical session data within MTU limits.
      Build verified clean (see below).
- [x] wifi_manager.c + ota_server.c — real implementations (not stubs) using plain
      esp_wifi_*/esp_http_server/esp_ota_ops calls, which work transparently over
      ESP-HOSTED once the C6 side is bridged (confirmed via Elecrow's own Lesson16 example:
      their bsp_wifi.c also just calls plain esp_wifi_* — esp_wifi_remote makes the
      hosted transport invisible to app code). Restructured partitions.csv from a single
      12MB "factory" app slot to real ota_0/ota_1 (6MB each) since esp_ota_get_next_update_partition()
      requires a proper OTA layout — verified safe: actual compiled app is only 1.27MB
      (10% of the old 12MB single slot), so 6MB per OTA slot has huge headroom.
- [x] CMakeLists/idf_component dependency updates + `idf.py build` verification — DONE, full
      clean build with 0 errors. `idf.py set-target esp32p4 && idf.py build` succeeds end to
      end. Final binary: 1.5MB (76% free in the 6MB ota_0 slot). Fixed along the way:
      missing LV_FONT_MONTSERRAT_24/28/36 Kconfig (an earlier edit landed in the
      auto-generated `sdkconfig` instead of the persistent `sdkconfig.defaults` and got
      wiped by a later `rm sdkconfig`), and the esp_wifi_remote EAP version-skew bug (above).

## Notes / gotchas discovered

- `handheld_crowpanel/main/rfid_driver.c` looks complete (128 lines, real frame
  parser) but `rfid_init()` never calls `uart_param_config`/`uart_driver_install`/
  `uart_set_pin`, and the parsing task is never started via `xTaskCreate`. It's a
  non-functional stub disguised as real code. GPIOs already chosen: RX=26, TX=29
  (UART_NUM_1, 9600 baud) — trusted as already hardware-validated by a prior session.
- `session_storage.c`: `session_get_tag`, `session_update_tag`, `session_list_tags`
  are empty stubs. Only aggregate `tag_count` is persisted per session, not the
  actual tag records. Needs full port from original's 781-line version.
- `ble_gatt_server.c` and button/feedback driver GPIOs (buttons 48/31/32, buzzer 47,
  vibrator 30) were already chosen/wired in a prior session — trusted as-is, not
  re-derived from CLAUDE.md's "TBD" notes (which are stale).
- Original is C++ (.cpp), CrowPanel port is plain C (.c) — established convention,
  keep following it (no classes, no references, use structs + free functions).
