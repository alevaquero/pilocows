#pragma once

// Show the BLE Sync modal overlay on top of the current screen.
void screen_ble_sync_show_modal(void);

// Called when language changes (no-op if modal is closed).
void screen_ble_sync_refresh_language(void);
