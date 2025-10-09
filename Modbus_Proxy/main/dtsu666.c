#include "dtsu666.h"
#include "modbus_rtu.h"
#include <string.h>
#include <math.h>

#define DTSU_BYTE_ORDER_ABCD 1

float dtsu666_parse_float32(const uint8_t* data, size_t offset) {
    union {
        uint32_t u;
        float f;
    } converter;

    converter.u = ((uint32_t)data[offset]     << 24) |
                  ((uint32_t)data[offset + 1] << 16) |
                  ((uint32_t)data[offset + 2] << 8)  |
                  ((uint32_t)data[offset + 3]);

    return converter.f;
}

void dtsu666_encode_float32(float value, uint8_t* data, size_t offset) {
    union {
        uint32_t u;
        float f;
    } converter;

    converter.f = value;

    data[offset]     = (converter.u >> 24) & 0xFF;
    data[offset + 1] = (converter.u >> 16) & 0xFF;
    data[offset + 2] = (converter.u >> 8)  & 0xFF;
    data[offset + 3] = converter.u         & 0xFF;
}

bool dtsu666_parse_response(const uint8_t* raw, uint16_t len, dtsu666_data_t* data) {
    if (!raw || len < 165 || !data) return false;

    const uint8_t* payload = raw + 3;
    const float power_scale = -1.0f;

    size_t offset = 0;

    data->current_L1 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->current_L2 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->current_L3 = dtsu666_parse_float32(payload, offset); offset += 4;

    data->voltage_LN_avg = dtsu666_parse_float32(payload, offset); offset += 4;
    data->voltage_L1N = dtsu666_parse_float32(payload, offset); offset += 4;
    data->voltage_L2N = dtsu666_parse_float32(payload, offset); offset += 4;
    data->voltage_L3N = dtsu666_parse_float32(payload, offset); offset += 4;

    data->voltage_LL_avg = dtsu666_parse_float32(payload, offset); offset += 4;
    data->voltage_L1L2 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->voltage_L2L3 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->voltage_L3L1 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->frequency = dtsu666_parse_float32(payload, offset); offset += 4;

    data->power_total = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;
    data->power_L1 = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;
    data->power_L2 = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;
    data->power_L3 = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;

    data->reactive_total = dtsu666_parse_float32(payload, offset); offset += 4;
    data->reactive_L1 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->reactive_L2 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->reactive_L3 = dtsu666_parse_float32(payload, offset); offset += 4;

    data->apparent_total = dtsu666_parse_float32(payload, offset); offset += 4;
    data->apparent_L1 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->apparent_L2 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->apparent_L3 = dtsu666_parse_float32(payload, offset); offset += 4;

    data->pf_total = dtsu666_parse_float32(payload, offset); offset += 4;
    data->pf_L1 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->pf_L2 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->pf_L3 = dtsu666_parse_float32(payload, offset); offset += 4;

    data->demand_total = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;
    data->demand_L1 = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;
    data->demand_L2 = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;
    data->demand_L3 = dtsu666_parse_float32(payload, offset) * power_scale; offset += 4;

    data->import_total = dtsu666_parse_float32(payload, offset); offset += 4;
    data->import_L1 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->import_L2 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->import_L3 = dtsu666_parse_float32(payload, offset); offset += 4;

    data->export_total = dtsu666_parse_float32(payload, offset); offset += 4;
    data->export_L1 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->export_L2 = dtsu666_parse_float32(payload, offset); offset += 4;
    data->export_L3 = dtsu666_parse_float32(payload, offset); offset += 4;

    return true;
}

bool dtsu666_apply_power_correction(uint8_t* raw, uint16_t len, float correction) {
    if (!raw || len < 165) return false;

    uint8_t* payload = raw + 3;
    union { uint32_t u; float f; } converter;

    float wallbox_power_per_phase = correction / 3.0f;

    uint8_t* power_l1_bytes = payload + 52;
    converter.u = ((uint32_t)power_l1_bytes[0] << 24) | ((uint32_t)power_l1_bytes[1] << 16) |
                  ((uint32_t)power_l1_bytes[2] << 8)  | (uint32_t)power_l1_bytes[3];
    converter.f += wallbox_power_per_phase;
    power_l1_bytes[0] = (converter.u >> 24) & 0xFF;
    power_l1_bytes[1] = (converter.u >> 16) & 0xFF;
    power_l1_bytes[2] = (converter.u >> 8)  & 0xFF;
    power_l1_bytes[3] = converter.u         & 0xFF;

    uint8_t* power_l2_bytes = payload + 56;
    converter.u = ((uint32_t)power_l2_bytes[0] << 24) | ((uint32_t)power_l2_bytes[1] << 16) |
                  ((uint32_t)power_l2_bytes[2] << 8)  | (uint32_t)power_l2_bytes[3];
    converter.f += wallbox_power_per_phase;
    power_l2_bytes[0] = (converter.u >> 24) & 0xFF;
    power_l2_bytes[1] = (converter.u >> 16) & 0xFF;
    power_l2_bytes[2] = (converter.u >> 8)  & 0xFF;
    power_l2_bytes[3] = converter.u         & 0xFF;

    uint8_t* power_l3_bytes = payload + 60;
    converter.u = ((uint32_t)power_l3_bytes[0] << 24) | ((uint32_t)power_l3_bytes[1] << 16) |
                  ((uint32_t)power_l3_bytes[2] << 8)  | (uint32_t)power_l3_bytes[3];
    converter.f += wallbox_power_per_phase;
    power_l3_bytes[0] = (converter.u >> 24) & 0xFF;
    power_l3_bytes[1] = (converter.u >> 16) & 0xFF;
    power_l3_bytes[2] = (converter.u >> 8)  & 0xFF;
    power_l3_bytes[3] = converter.u         & 0xFF;

    uint8_t* power_bytes = payload + 48;
    converter.u = ((uint32_t)power_bytes[0] << 24) | ((uint32_t)power_bytes[1] << 16) |
                  ((uint32_t)power_bytes[2] << 8)  | (uint32_t)power_bytes[3];
    converter.f += correction;
    power_bytes[0] = (converter.u >> 24) & 0xFF;
    power_bytes[1] = (converter.u >> 16) & 0xFF;
    power_bytes[2] = (converter.u >> 8)  & 0xFF;
    power_bytes[3] = converter.u         & 0xFF;

    uint8_t* demand_l1_bytes = payload + 116;
    converter.u = ((uint32_t)demand_l1_bytes[0] << 24) | ((uint32_t)demand_l1_bytes[1] << 16) |
                  ((uint32_t)demand_l1_bytes[2] << 8)  | (uint32_t)demand_l1_bytes[3];
    converter.f += wallbox_power_per_phase;
    demand_l1_bytes[0] = (converter.u >> 24) & 0xFF;
    demand_l1_bytes[1] = (converter.u >> 16) & 0xFF;
    demand_l1_bytes[2] = (converter.u >> 8)  & 0xFF;
    demand_l1_bytes[3] = converter.u         & 0xFF;

    uint8_t* demand_l2_bytes = payload + 120;
    converter.u = ((uint32_t)demand_l2_bytes[0] << 24) | ((uint32_t)demand_l2_bytes[1] << 16) |
                  ((uint32_t)demand_l2_bytes[2] << 8)  | (uint32_t)demand_l2_bytes[3];
    converter.f += wallbox_power_per_phase;
    demand_l2_bytes[0] = (converter.u >> 24) & 0xFF;
    demand_l2_bytes[1] = (converter.u >> 16) & 0xFF;
    demand_l2_bytes[2] = (converter.u >> 8)  & 0xFF;
    demand_l2_bytes[3] = converter.u         & 0xFF;

    uint8_t* demand_l3_bytes = payload + 124;
    converter.u = ((uint32_t)demand_l3_bytes[0] << 24) | ((uint32_t)demand_l3_bytes[1] << 16) |
                  ((uint32_t)demand_l3_bytes[2] << 8)  | (uint32_t)demand_l3_bytes[3];
    converter.f += wallbox_power_per_phase;
    demand_l3_bytes[0] = (converter.u >> 24) & 0xFF;
    demand_l3_bytes[1] = (converter.u >> 16) & 0xFF;
    demand_l3_bytes[2] = (converter.u >> 8)  & 0xFF;
    demand_l3_bytes[3] = converter.u         & 0xFF;

    uint8_t* demand_bytes = payload + 112;
    converter.u = ((uint32_t)demand_bytes[0] << 24) | ((uint32_t)demand_bytes[1] << 16) |
                  ((uint32_t)demand_bytes[2] << 8)  | (uint32_t)demand_bytes[3];
    converter.f += correction;
    demand_bytes[0] = (converter.u >> 24) & 0xFF;
    demand_bytes[1] = (converter.u >> 16) & 0xFF;
    demand_bytes[2] = (converter.u >> 8)  & 0xFF;
    demand_bytes[3] = converter.u         & 0xFF;

    uint16_t new_crc = modbus_crc16(raw, len - 2);
    raw[len - 2] = new_crc & 0xFF;
    raw[len - 1] = (new_crc >> 8) & 0xFF;

    return true;
}
