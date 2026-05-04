#include "rfid_reader.h"
#include "board_config.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "rfid";

// ---------------------------------------------------------------------------
// WL-134 packet format (30 bytes total, 9600 baud, 8N1)
//
//  [0]     0x02  STX
//  [1..10] 10 ASCII hex chars — animal ID, LSB-first (5 bytes / 40 bits raw)
//  [11..14] 4 ASCII hex chars — country code, LSB-first (2 bytes / 10 bits)
//  [15]    ASCII '0'/'1' — data flag
//  [16]    ASCII '0'/'1' — animal flag
//  [17..20] 4 ASCII hex chars — reserved0
//  [21..26] 6 ASCII hex chars — reserved1
//  [27]    XOR checksum of bytes [1..26]
//  [28]    ~checksum (one's complement of byte 27)
//  [29]    0x03  ETX
//
// Final tag string: sprintf("%03d%012lld", country, animal_id)
// Example: country=900, id=158002044774 → "900158002044774"
// ---------------------------------------------------------------------------

#define RFID_PACKET_SIZE      30
#define RFID_PACKET_STX       0x02
#define RFID_PACKET_ETX       0x03

#define RFID_OFF_ID           1
#define RFID_OFF_COUNTRY      11
#define RFID_OFF_DATA_FLAG    15
#define RFID_OFF_ANIMAL_FLAG  16
#define RFID_OFF_CHECKSUM     27
#define RFID_OFF_CHK_INV      28
#define RFID_OFF_ETX          29

#define RFID_BUF_SIZE         128
#define RFID_TASK_STACK       4096
#define RFID_TASK_PRIO        5

static rfid_tag_callback_t s_callback = NULL;
static TaskHandle_t        s_task     = NULL;

// ---------------------------------------------------------------------------
// Decode an ASCII hex string stored LSB-first into a uint64.
// e.g. "66FD7A9C42" (10 chars) → read right-to-left as nibbles → 0x24C9A7DF66
// ---------------------------------------------------------------------------
static uint64_t hex_lsb_ascii_to_uint64(const uint8_t *text, uint8_t len)
{
    uint64_t value = 0;
    uint8_t i = len;
    do {
        i--;
        uint8_t nibble = text[i];
        nibble = (nibble >= 'A') ? (nibble - 'A' + 10) : (nibble - '0');
        value = (value << 4) + nibble;
    } while (i != 0);
    return value;
}

// ---------------------------------------------------------------------------
// Parse one complete 30-byte WL-134 packet into an EID string.
// Returns true on success.
// ---------------------------------------------------------------------------
static bool parse_packet(const uint8_t *pkt, rfid_tag_t *tag_out)
{
    if (pkt[0] != RFID_PACKET_STX || pkt[RFID_OFF_ETX] != RFID_PACKET_ETX) {
        ESP_LOGW(TAG, "Bad framing: STX=%02X ETX=%02X", pkt[0], pkt[RFID_OFF_ETX]);
        return false;
    }

    // Verify XOR checksum over bytes [1..26]
    uint8_t checksum = 0;
    for (int i = RFID_OFF_ID; i < RFID_OFF_CHECKSUM; i++) {
        checksum ^= pkt[i];
    }
    if (checksum != pkt[RFID_OFF_CHECKSUM]) {
        ESP_LOGW(TAG, "Checksum mismatch: calc=%02X pkt=%02X", checksum, pkt[RFID_OFF_CHECKSUM]);
        return false;
    }
    if ((uint8_t)(~checksum) != pkt[RFID_OFF_CHK_INV]) {
        ESP_LOGW(TAG, "Inverted checksum mismatch: ~calc=%02X pkt=%02X",
                 (uint8_t)(~checksum), pkt[RFID_OFF_CHK_INV]);
        return false;
    }

    uint64_t animal_id = hex_lsb_ascii_to_uint64(&pkt[RFID_OFF_ID],      10);
    uint64_t country   = hex_lsb_ascii_to_uint64(&pkt[RFID_OFF_COUNTRY],   4);
    bool     is_data   = pkt[RFID_OFF_DATA_FLAG]   == '1';
    bool     is_animal = pkt[RFID_OFF_ANIMAL_FLAG]  == '1';

    ESP_LOGI(TAG, "animal_id=%012llu  country=%03llu  data=%d  animal=%d",
             (unsigned long long)animal_id, (unsigned long long)country,
             is_data, is_animal);

    snprintf(tag_out->eid, sizeof(tag_out->eid), "%03llu%012llu",
             (unsigned long long)country, (unsigned long long)animal_id);

    return true;
}

// ---------------------------------------------------------------------------
// RFID reader task — runs continuously, decodes packets as they arrive
// ---------------------------------------------------------------------------
static void rfid_task(void *arg)
{
    uint8_t buf[RFID_BUF_SIZE];
    int     stored = 0;   // bytes accumulated in buf waiting for a full packet

    while (true) {
        int n = uart_read_bytes(RFID_UART_PORT, buf + stored,
                                sizeof(buf) - stored - 1,
                                pdMS_TO_TICKS(200));
        if (n <= 0) continue;

        stored += n;

        // Scan accumulated buffer for complete packets
        int consumed = 0;
        while (stored - consumed >= RFID_PACKET_SIZE) {
            // Find STX
            if (buf[consumed] != RFID_PACKET_STX) {
                ESP_LOGW(TAG, "Skipping non-STX byte: %02X", buf[consumed]);
                consumed++;
                continue;
            }

            const uint8_t *pkt = &buf[consumed];
            rfid_tag_t tag;
            if (parse_packet(pkt, &tag)) {
                ESP_LOGI(TAG, "Tag: %s", tag.eid);
                if (s_callback) s_callback(&tag);
            }
            consumed += RFID_PACKET_SIZE;
        }

        // Shift leftover bytes to the front of the buffer
        if (consumed > 0) {
            stored -= consumed;
            if (stored > 0) memmove(buf, buf + consumed, stored);
        }
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void rfid_init(rfid_tag_callback_t on_tag_read)
{
    s_callback = on_tag_read;

    // ESP-IDF UART init order: param_config → set_pin → driver_install
    uart_config_t uart_cfg = {
        .baud_rate  = RFID_BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_param_config(RFID_UART_PORT, &uart_cfg);
    uart_set_pin(RFID_UART_PORT, UART_PIN_NO_CHANGE, RFID_PIN_RX,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(RFID_UART_PORT, RFID_BUF_SIZE * 2, 0, 0, NULL, 0);

    xTaskCreate(rfid_task, "rfid", RFID_TASK_STACK, NULL, RFID_TASK_PRIO, &s_task);
    ESP_LOGI(TAG, "RFID reader ready — UART%d RX=GPIO%d @ %d baud",
             RFID_UART_PORT, RFID_PIN_RX, RFID_BAUD_RATE);
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
    (void)enabled;  // WL-134 scans continuously; no enable/disable command
}
