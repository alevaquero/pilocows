#pragma once

// Full-screen WiFi configuration.
// Scans for networks on load, lets the user pick an SSID from a dropdown,
// enter the password via the on-screen keyboard, and tap Connect.
void screen_wifi_create(void);
void screen_wifi_load(void);
void screen_wifi_refresh_language(void);
