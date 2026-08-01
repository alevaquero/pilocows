#ifndef _BUTTON_DRIVER_H_
#define _BUTTON_DRIVER_H_

#include "esp_err.h"

typedef enum {
    BUTTON_UP = 0,
    BUTTON_DOWN = 1,
    BUTTON_SELECT = 2,
    BUTTON_COUNT = 3
} button_id_t;

typedef void (*button_callback_t)(button_id_t button, int pressed);

// Initialize button driver (interrupt-based with debounce)
esp_err_t button_init(button_callback_t callback);

// Deinitialize button driver
esp_err_t button_deinit(void);

// Get current button state (0=released, 1=pressed)
int button_get_state(button_id_t button);

#endif
