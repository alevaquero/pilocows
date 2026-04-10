#include "rfid_reader.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "rfid";

// ---------------------------------------------------------------------------
// Protocol notes for the 134.2K AGV FDX-B ISO11784/85 UART reader module
//
// The module outputs a packet when a tag enters range (auto-send mode).
// Common packet format (10 bytes binary + framing):
//   D6 00 75 [10 bytes EID little-endian] [1 byte checksum]
//
// The EID is a 64-bit value stored in bytes 3–10 (little-endian).
// We extract it and format as a 15-digit decimal string.
//
// NOTE: Adjust RFID_PACKET_HEADER and parsing below if your module's
// datasheet specifies a different frame format.
// ---------------------------------------------------------------------------

#define RFID_BUF_SIZE        128
#define RFID_PACKET_HEADER   0xD6
#define RFID_PACKET_LEN      13   // Header(1) + len(1) + cmd(1) + EID(8) + country(2) + crc(1) — adjust as needed
#define RFID_TASK_STACK      4096
#define RFID_TASK_PRIO       5

static rfid_tag_callback_t s_callback = NULL;
static TaskHandle_t        s_task     = NULL;
static bool                s_enabled  = true;

// Parse raw 8-byte FDX-B EID (little-endian) into a 15-digit decimal string.
// FDX-B: 38 bits individual number + 10 bits country code + 16 reserved bits.
static void parse_fdxb(const uint8_t *raw8, rfid_tag_t *tag_out)
{
    // Reconstruct 64-bit value (little-endian)
    uint64_t raw64 = 0;
    for (int i = 7; i >= 0; i--) {
        raw64 = (raw64 << 8) | raw8[i];
    }
    // FDX-B bit layout (LSB first in the air, but we have MSB-first after reorder):
    // bits 0–37:  individual number (38 bits)
    // bits 38–47: country code (10 bits)
    // We display the full 64-bit numeric value as a 15-digit string
    snprintf(tag_out->eid, sizeof(tag_out->eid), "%015llu", (unsigned long long)raw64);
}

// Verify simple XOR checksum (adjust if module uses a different scheme)
static bool verify_checksum(const uint8_t *buf, int len)
{
    uint8_t crc = 0;
    for (int i = 0; i < len - 1; i++) {
        crc ^= buf[i];
    }
    return crc == buf[len - 1];
}

static void rfid_task(void *arg)
{
    uint8_t buf[RFID_BUF_SIZE];

    while (true) {
        if (!s_enabled) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int len = uart_read_bytes(RFID_UART_PORT, buf, sizeof(buf) - 1,
                                  pdMS_TO_TICKS(200));
        if (len <= 0) continue;

        // Scan buffer for packet header
        for (int i = 0; i < len; i++) {
            if (buf[i] != RFID_PACKET_HEADER) continue;
            if (i + RFID_PACKET_LEN > len) break;  // Incomplete packet

            const uint8_t *pkt = &buf[i];
            if (!verify_checksum(pkt, RFID_PACKET_LEN)) {
                ESP_LOGW(TAG, "Checksum mismatch — skipping packet");
                continue;
            }

            // EID bytes start at offset 3 (after header, length, command bytes)
            rfid_tag_t tag;
            parse_fdxb(&pkt[3], &tag);

            ESP_LOGI(TAG, "Tag: %s", tag.eid);

            if (s_callback) {
                s_callback(&tag);
            }

            i += RFID_PACKET_LEN - 1;  // Advance past this packet
        }
    }
}

void rfid_init(rfid_tag_callback_t on_tag_read)
{
    s_callback = on_tag_read;

    uart_config_t uart_cfg = {
        .baud_rate  = RFID_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(RFID_UART_PORT, RFID_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(RFID_UART_PORT, &uart_cfg);
    uart_set_pin(RFID_UART_PORT, RFID_PIN_TX, RFID_PIN_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    xTaskCreate(rfid_task, "rfid", RFID_TASK_STACK, NULL, RFID_TASK_PRIO, &s_task);
    ESP_LOGI(TAG, "RFID reader ready on UART%d (RX=%d TX=%d @ %d baud)",
             RFID_UART_PORT, RFID_PIN_RX, RFID_PIN_TX, RFID_BAUD_RATE);
}

void rfid_deinit(void)
{
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    uart_driver_delete(RFID_UART_PORT);
}

void rfid_set_scanning(bool enabled)
{
    s_enabled = enabled;
}
