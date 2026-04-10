#pragma once

// =============================================================================
// Pilocows Handheld — Board Pin Configuration
// Board: ZX3D50CE02S-USRC-4832 (SC01 Plus) — WT32-S3-WROVER-2-N8R2
// Datasheet: docs/ZX3D50CE02S-USRC-4832+Datasheet.pdf
// =============================================================================

// -----------------------------------------------------------------------------
// LCD — ST7796UI, 480x320, RGB565, 8-bit 8080 parallel interface
// -----------------------------------------------------------------------------
#define LCD_PIN_BL          45   // Backlight PWM, active high
#define LCD_PIN_RESET        4   // Reset (shared with touch reset)
#define LCD_PIN_RS           0   // Register Select / D/C
#define LCD_PIN_WR          47   // Write clock
#define LCD_PIN_TE          48   // Tearing effect / frame sync
#define LCD_PIN_DB0          9   // Data bus bit 0
#define LCD_PIN_DB1         46
#define LCD_PIN_DB2          3
#define LCD_PIN_DB3          8
#define LCD_PIN_DB4         18
#define LCD_PIN_DB5         17
#define LCD_PIN_DB6         16
#define LCD_PIN_DB7         15   // Data bus bit 7

#define LCD_H_RES          480
#define LCD_V_RES          320
#define LCD_BITS_PER_PIXEL  16   // RGB565
#define LCD_PCLK_HZ         (20 * 1000 * 1000)

// -----------------------------------------------------------------------------
// Touch — FT6336U, I2C (single touch)
// NOTE: TP_RST is shared with LCD_PIN_RESET — toggled together during init
// -----------------------------------------------------------------------------
#define TOUCH_PIN_SDA        6
#define TOUCH_PIN_SCL        5
#define TOUCH_PIN_INT        7
#define TOUCH_PIN_RST        4   // Shared with LCD_PIN_RESET

#define TOUCH_I2C_PORT       I2C_NUM_0
#define TOUCH_I2C_FREQ_HZ    400000

// -----------------------------------------------------------------------------
// Audio — I2S amplifier (2.5W, onboard speaker connector)
// TODO: Wire speaker to SPK+/SPK- connector before enabling
// -----------------------------------------------------------------------------
#define AUDIO_PIN_LRCK      35
#define AUDIO_PIN_BCLK      36
#define AUDIO_PIN_DOUT      37
// #define AUDIO_ENABLED           // Uncomment when speaker is wired

// -----------------------------------------------------------------------------
// SD Card — SPI interface
// -----------------------------------------------------------------------------
#define SD_PIN_CS           41
#define SD_PIN_MOSI         40
#define SD_PIN_CLK          39
#define SD_PIN_MISO         38

// -----------------------------------------------------------------------------
// RS485 — through onboard transceiver chip
// (Not used for RFID — transceiver chip is in the signal path)
// -----------------------------------------------------------------------------
#define RS485_PIN_RXD        1
#define RS485_PIN_RTS        2
#define RS485_PIN_TXD       42

// -----------------------------------------------------------------------------
// Extended IO Header — our custom peripherals
// -----------------------------------------------------------------------------

// RFID reader module — UART1
#define RFID_PIN_RX         10   // EXT_IO1 — receive from RFID module
#define RFID_PIN_TX         11   // EXT_IO2 — send to RFID module
#define RFID_UART_PORT      UART_NUM_1
#define RFID_BAUD_RATE       9600

// Mechanical buttons (active low, internal pull-up)
#define BTN_PIN_SCAN        12   // EXT_IO3 — dedicated scan button
#define BTN_PIN_UP          13   // EXT_IO4 — navigate up / back
#define BTN_PIN_DOWN        14   // EXT_IO5 — navigate down / confirm

// Vibrator motor — NOT YET WIRED
// TODO: Connect vibrator motor driver to EXT_IO6
#define VIBRATOR_PIN        21   // EXT_IO6
// #define VIBRATOR_ENABLED        // Uncomment when motor is wired

// -----------------------------------------------------------------------------
// Miscellaneous
// -----------------------------------------------------------------------------
#define LCD_BACKLIGHT_ON_LEVEL   1   // Active high
#define BTN_DEBOUNCE_MS         50
