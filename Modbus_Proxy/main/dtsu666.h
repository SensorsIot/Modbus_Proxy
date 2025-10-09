#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "modbus_rtu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    float current_L1, current_L2, current_L3;
    float voltage_LN_avg, voltage_L1N, voltage_L2N, voltage_L3N;
    float voltage_LL_avg, voltage_L1L2, voltage_L2L3, voltage_L3L1;
    float frequency;
    float power_total, power_L1, power_L2, power_L3;
    float reactive_total, reactive_L1, reactive_L2, reactive_L3;
    float apparent_total, apparent_L1, apparent_L2, apparent_L3;
    float pf_total, pf_L1, pf_L2, pf_L3;
    float demand_total, demand_L1, demand_L2, demand_L3;
    float import_total, import_L1, import_L2, import_L3;
    float export_total, export_L1, export_L2, export_L3;
} dtsu666_data_t;

typedef struct {
    SemaphoreHandle_t mutex;
    bool valid;
    uint32_t timestamp;
    uint8_t response_buffer[165];
    uint16_t response_length;
    dtsu666_data_t parsed_data;
    uint32_t update_count;
} shared_dtsu_data_t;

bool dtsu666_parse_response(const uint8_t* raw, uint16_t len, dtsu666_data_t* data);
bool dtsu666_apply_power_correction(uint8_t* raw, uint16_t len, float correction);
float dtsu666_parse_float32(const uint8_t* data, size_t offset);
void dtsu666_encode_float32(float value, uint8_t* data, size_t offset);
