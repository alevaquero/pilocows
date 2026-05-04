#pragma once

#include <stdbool.h>

typedef enum {
    BTN_SCAN = 0,
    BTN_COUNT,
} button_id_t;

typedef enum {
    BTN_EVENT_PRESS,
    BTN_EVENT_RELEASE,
    BTN_EVENT_LONG_PRESS,  // Held > 1s
} button_event_t;

typedef void (*button_callback_t)(button_id_t btn, button_event_t event);

// Initialize all mechanical buttons with debounce + long-press detection.
void buttons_init(button_callback_t on_event);

// Poll-based check — call from main task if not using interrupt-driven mode.
void buttons_poll(void);
