#include "modbus_rtu.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char* TAG = "MODBUS_RTU";

void modbus_rtu_init(modbus_rtu_t* mb, uart_port_t uart_num, int tx_pin, int rx_pin, uint32_t baud) {
    mb->uart_num = uart_num;
    mb->baud = baud ? baud : 9600;
    mb->buf_len = 0;

    mb->t_char_us = (11 * 1000000) / mb->baud;
    if (mb->t_char_us == 0) mb->t_char_us = 1000;

    mb->t3_5_us = (uint32_t)(3.5f * mb->t_char_us) + 2;
    mb->t1_5_us = (uint32_t)(1.5f * mb->t_char_us) + 2;

    uart_config_t uart_config = {
        .baud_rate = mb->baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(uart_num, MODBUS_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ESP_LOGI(TAG, "UART%d initialized: %lu baud, TX=%d, RX=%d", uart_num, (unsigned long)mb->baud, tx_pin, rx_pin);
}

uint16_t modbus_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool modbus_validate_crc(const uint8_t* data, size_t len) {
    if (len < 2) return false;

    uint16_t crc_given = (uint16_t)data[len-2] | ((uint16_t)data[len-1] << 8);
    uint16_t crc_calc = modbus_crc16(data, len - 2);

    return crc_given == crc_calc;
}

bool modbus_parse(const uint8_t* frame, uint16_t len, modbus_message_t* msg) {
    memset(msg, 0, sizeof(modbus_message_t));

    if (len < 4) return false;

    if (!modbus_validate_crc(frame, len)) return false;

    msg->valid = true;
    msg->id = frame[0];
    msg->fc = frame[1];
    msg->len = len;
    msg->raw = frame;

    if ((msg->fc & 0x80) && len >= 5) {
        msg->type = MB_TYPE_EXCEPTION;
        msg->fc &= 0x7F;
        msg->exCode = frame[2];
        return true;
    }

    switch (msg->fc) {
        case 0x03:
        case 0x04:
            if (len == 8) {
                msg->type = MB_TYPE_REQUEST;
                msg->startAddr = modbus_be16(&frame[2]);
                msg->qty = modbus_be16(&frame[4]);
            } else {
                uint8_t bc = frame[2];
                if (len == (uint16_t)bc + 5) {
                    msg->type = MB_TYPE_REPLY;
                    msg->byteCount = bc;
                } else {
                    msg->type = MB_TYPE_UNKNOWN;
                }
            }
            break;

        case 0x06:
            if (len == 8) {
                msg->type = MB_TYPE_REQUEST;
                msg->wrAddr = modbus_be16(&frame[2]);
                msg->wrValue = modbus_be16(&frame[4]);
            } else {
                msg->type = MB_TYPE_UNKNOWN;
            }
            break;

        case 0x10:
            if (len == 8) {
                msg->type = MB_TYPE_REPLY;
                msg->wrAddr = modbus_be16(&frame[2]);
                msg->wrQty = modbus_be16(&frame[4]);
            } else if (len >= 9) {
                uint8_t bc = frame[6];
                if (len == (uint16_t)bc + 9) {
                    msg->type = MB_TYPE_REQUEST;
                    msg->wrAddr = modbus_be16(&frame[2]);
                    msg->wrQty = modbus_be16(&frame[4]);
                    msg->wrByteCount = bc;
                } else {
                    msg->type = MB_TYPE_UNKNOWN;
                }
            } else {
                msg->type = MB_TYPE_UNKNOWN;
            }
            break;

        default:
            msg->type = MB_TYPE_UNKNOWN;
            break;
    }

    return true;
}

bool modbus_read(modbus_rtu_t* mb, modbus_message_t* msg, uint32_t timeout_ms) {
    mb->buf_len = 0;
    int64_t start_time = esp_timer_get_time() / 1000;

    uint8_t data;
    int len = uart_read_bytes(mb->uart_num, &data, 1, pdMS_TO_TICKS(timeout_ms));
    if (len <= 0) return false;

    mb->buf[mb->buf_len++] = data;
    int64_t last_char_time = esp_timer_get_time();

    while (true) {
        if (timeout_ms && ((esp_timer_get_time() / 1000) - start_time >= timeout_ms)) {
            return false;
        }

        len = uart_read_bytes(mb->uart_num, &data, 1, 0);
        if (len > 0) {
            if (mb->buf_len < MODBUS_BUF_SIZE) {
                mb->buf[mb->buf_len++] = data;
            }
            last_char_time = esp_timer_get_time();
        }

        if ((esp_timer_get_time() - last_char_time) >= mb->t3_5_us) {
            break;
        }

        esp_rom_delay_us(50);
    }

    if (!modbus_parse(mb->buf, mb->buf_len, msg)) {
        return false;
    }

    msg->raw = mb->buf;
    return true;
}

bool modbus_write_raw(modbus_rtu_t* mb, const uint8_t* data, size_t len) {
    if (!data || len < 2) return false;

    esp_rom_delay_us(mb->t3_5_us);

    int written = uart_write_bytes(mb->uart_num, data, len);
    if (written != len) return false;

    uart_wait_tx_done(mb->uart_num, pdMS_TO_TICKS(100));

    return true;
}

bool modbus_write_msg(modbus_rtu_t* mb, const modbus_message_t* msg) {
    if (!msg || !msg->valid || !msg->raw) return false;
    return modbus_write_raw(mb, msg->raw, msg->len);
}
