#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "driver/uart.h"

#define MODBUS_BUF_SIZE 256

typedef enum {
    MB_TYPE_UNKNOWN = 0,
    MB_TYPE_REQUEST,
    MB_TYPE_REPLY,
    MB_TYPE_EXCEPTION
} modbus_msg_type_t;

typedef struct {
    bool valid;
    uint8_t id;
    uint8_t fc;
    modbus_msg_type_t type;
    uint16_t len;

    uint16_t startAddr;
    uint16_t qty;
    uint8_t byteCount;

    uint16_t wrAddr;
    uint16_t wrValue;
    uint16_t wrQty;
    uint8_t wrByteCount;

    uint8_t exCode;

    const uint8_t* raw;
} modbus_message_t;

typedef struct {
    uart_port_t uart_num;
    uint32_t baud;
    uint32_t t_char_us;
    uint32_t t3_5_us;
    uint32_t t1_5_us;
    uint8_t buf[MODBUS_BUF_SIZE];
    uint16_t buf_len;
} modbus_rtu_t;

void modbus_rtu_init(modbus_rtu_t* mb, uart_port_t uart_num, int tx_pin, int rx_pin, uint32_t baud);
uint16_t modbus_crc16(const uint8_t* data, size_t len);
bool modbus_parse(const uint8_t* frame, uint16_t len, modbus_message_t* msg);
bool modbus_read(modbus_rtu_t* mb, modbus_message_t* msg, uint32_t timeout_ms);
bool modbus_write_raw(modbus_rtu_t* mb, const uint8_t* data, size_t len);
bool modbus_write_msg(modbus_rtu_t* mb, const modbus_message_t* msg);
bool modbus_validate_crc(const uint8_t* data, size_t len);

static inline uint16_t modbus_be16(const uint8_t* p) {
    return ((uint16_t)p[0] << 8) | p[1];
}
