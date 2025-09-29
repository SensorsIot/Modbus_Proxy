#include "dtsu666.h"

// Register name mapping for debugging
const char* dtsuRegisterNames[40] = {
  "I_L1", "I_L2", "I_L3",
  "U_LN_AVG", "U_L1N", "U_L2N", "U_L3N",
  "U_LL_AVG", "U_L1L2", "U_L2L3", "U_L3L1", "FREQ",
  "P_TOT(-)", "P_L1(-)", "P_L2(-)", "P_L3(-)",
  "Q_TOT", "Q_L1", "Q_L2", "Q_L3",
  "S_TOT", "S_L1", "S_L2", "S_L3",
  "PF_TOT", "PF_L1", "PF_L2", "PF_L3",
  "DMD_TOT(-)", "DMD_L1(-)", "DMD_L2(-)", "DMD_L3(-)",
  "E_IMP_T", "E_IMP_L1", "E_IMP_L2", "E_IMP_L3",
  "E_EXP_T", "E_EXP_L1", "E_EXP_L2", "E_EXP_L3"
};

// Byte order definitions for IEEE 754 float parsing
#define DTSU_BYTE_ORDER_ABCD 1   // Big Endian: A B C D (Most common)
#define DTSU_BYTE_ORDER_DCBA 2   // Little Endian: D C B A
#define DTSU_BYTE_ORDER_BADC 3   // Mid-Big Endian: B A D C
#define DTSU_BYTE_ORDER_CDAB 4   // Mid-Little Endian: C D A B

// Current byte order setting
#define DTSU_CURRENT_ORDER DTSU_BYTE_ORDER_ABCD

int16_t parseInt16(const uint8_t* data, size_t offset) {
  return (int16_t)((data[offset] << 8) | data[offset + 1]);
}

uint16_t parseUInt16(const uint8_t* data, size_t offset) {
  return (uint16_t)((data[offset] << 8) | data[offset + 1]);
}

float parseFloat32(const uint8_t* data, size_t offset) {
  union {
    uint32_t u;
    float f;
  } converter;

#if DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_ABCD
  converter.u = ((uint32_t)data[offset]     << 24) |
                ((uint32_t)data[offset + 1] << 16) |
                ((uint32_t)data[offset + 2] << 8)  |
                ((uint32_t)data[offset + 3]);
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_DCBA
  converter.u = ((uint32_t)data[offset + 3] << 24) |
                ((uint32_t)data[offset + 2] << 16) |
                ((uint32_t)data[offset + 1] << 8)  |
                ((uint32_t)data[offset]);
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_BADC
  converter.u = ((uint32_t)data[offset + 1] << 24) |
                ((uint32_t)data[offset]     << 16) |
                ((uint32_t)data[offset + 3] << 8)  |
                ((uint32_t)data[offset + 2]);
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_CDAB
  converter.u = ((uint32_t)data[offset + 2] << 24) |
                ((uint32_t)data[offset + 3] << 16) |
                ((uint32_t)data[offset]     << 8)  |
                ((uint32_t)data[offset + 1]);
#endif

  return converter.f;
}

void encodeFloat32(float value, uint8_t* data, size_t offset) {
  union {
    uint32_t u;
    float f;
  } converter;

  converter.f = value;

#if DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_ABCD
  data[offset]     = (converter.u >> 24) & 0xFF;
  data[offset + 1] = (converter.u >> 16) & 0xFF;
  data[offset + 2] = (converter.u >> 8)  & 0xFF;
  data[offset + 3] = converter.u         & 0xFF;
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_DCBA
  data[offset]     = converter.u         & 0xFF;
  data[offset + 1] = (converter.u >> 8)  & 0xFF;
  data[offset + 2] = (converter.u >> 16) & 0xFF;
  data[offset + 3] = (converter.u >> 24) & 0xFF;
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_BADC
  data[offset]     = (converter.u >> 16) & 0xFF;
  data[offset + 1] = (converter.u >> 24) & 0xFF;
  data[offset + 2] = converter.u         & 0xFF;
  data[offset + 3] = (converter.u >> 8)  & 0xFF;
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_CDAB
  data[offset]     = (converter.u >> 8)  & 0xFF;
  data[offset + 1] = converter.u         & 0xFF;
  data[offset + 2] = (converter.u >> 24) & 0xFF;
  data[offset + 3] = (converter.u >> 16) & 0xFF;
#endif
}

bool parseDTSU666Data(uint16_t startAddr, const ModbusMessage& msg, DTSU666Data& data) {
  if (!msg.valid || msg.type != MBType::Reply || !msg.raw) {
    return false;
  }

  const uint8_t* payload = msg.raw + 3;
  uint8_t payloadSize = msg.raw[2];

  if (payloadSize != 160) {
    return false;
  }

  const float volt_scale = 1.0f;
  const float amp_scale = 1.0f;
  const float power_scale = -1.0f;

  size_t offset = 0;

  data.current_L1 = parseFloat32(payload, offset) * amp_scale; offset += 4;
  data.current_L2 = parseFloat32(payload, offset) * amp_scale; offset += 4;
  data.current_L3 = parseFloat32(payload, offset) * amp_scale; offset += 4;

  data.voltage_LN_avg = parseFloat32(payload, offset) * volt_scale; offset += 4;
  data.voltage_L1N = parseFloat32(payload, offset) * volt_scale; offset += 4;
  data.voltage_L2N = parseFloat32(payload, offset) * volt_scale; offset += 4;
  data.voltage_L3N = parseFloat32(payload, offset) * volt_scale; offset += 4;

  data.voltage_LL_avg = parseFloat32(payload, offset) * volt_scale; offset += 4;
  data.voltage_L1L2 = parseFloat32(payload, offset) * volt_scale; offset += 4;
  data.voltage_L2L3 = parseFloat32(payload, offset) * volt_scale; offset += 4;
  data.voltage_L3L1 = parseFloat32(payload, offset) * volt_scale; offset += 4;
  data.frequency = parseFloat32(payload, offset); offset += 4;

  data.power_total = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.power_L1 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.power_L2 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.power_L3 = parseFloat32(payload, offset) * power_scale; offset += 4;

  data.reactive_total = parseFloat32(payload, offset); offset += 4;
  data.reactive_L1 = parseFloat32(payload, offset); offset += 4;
  data.reactive_L2 = parseFloat32(payload, offset); offset += 4;
  data.reactive_L3 = parseFloat32(payload, offset); offset += 4;

  data.apparent_total = parseFloat32(payload, offset); offset += 4;
  data.apparent_L1 = parseFloat32(payload, offset); offset += 4;
  data.apparent_L2 = parseFloat32(payload, offset); offset += 4;
  data.apparent_L3 = parseFloat32(payload, offset); offset += 4;

  data.pf_total = parseFloat32(payload, offset); offset += 4;
  data.pf_L1 = parseFloat32(payload, offset); offset += 4;
  data.pf_L2 = parseFloat32(payload, offset); offset += 4;
  data.pf_L3 = parseFloat32(payload, offset); offset += 4;

  data.demand_total = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.demand_L1 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.demand_L2 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.demand_L3 = parseFloat32(payload, offset) * power_scale; offset += 4;

  data.import_total = parseFloat32(payload, offset); offset += 4;
  data.import_L1 = parseFloat32(payload, offset); offset += 4;
  data.import_L2 = parseFloat32(payload, offset); offset += 4;
  data.import_L3 = parseFloat32(payload, offset); offset += 4;

  data.export_total = parseFloat32(payload, offset); offset += 4;
  data.export_L1 = parseFloat32(payload, offset); offset += 4;
  data.export_L2 = parseFloat32(payload, offset); offset += 4;
  data.export_L3 = parseFloat32(payload, offset); offset += 4;

  return true;
}

static inline uint16_t be16u(const uint8_t* p) {
  return (uint16_t(p[0]) << 8) | p[1];
}

bool parseDTSU666MetaWords(uint16_t startAddr, const ModbusMessage& msg, DTSU666Meta& meta) {
  if (!msg.valid || msg.type != MBType::Reply || !msg.raw) return false;

  const uint8_t* payload = msg.raw + 3;
  size_t o = 0;

  if (startAddr == 2001 && msg.raw[2] >= 2) {
    meta.status = be16u(payload);
    return true;
  }

  if (startAddr == DTSU_VERSION_REG && msg.raw[2] >= 20) {
    meta.version          = be16u(&payload[o]); o += 2;
    meta.passcode         = be16u(&payload[o]); o += 2;
    meta.zero_clear_flag  = be16u(&payload[o]); o += 2;
    meta.connection_mode  = be16u(&payload[o]); o += 2;
    meta.irat             = be16u(&payload[o]); o += 2;
    meta.urat             = be16u(&payload[o]); o += 2;
    meta.protocol         = be16u(&payload[o]); o += 2;
    meta.address          = be16u(&payload[o]); o += 2;
    meta.baud             = be16u(&payload[o]); o += 2;
    meta.meter_type       = be16u(&payload[o]);
    return true;
  }

  return false;
}

bool encodeDTSU666Response(const DTSU666Data& data, uint8_t* buffer, size_t bufferSize) {
  if (bufferSize < 165) {
    return false;
  }

  buffer[0] = 0x0B;
  buffer[1] = 0x03;
  buffer[2] = 0xA0;

  size_t offset = 0;
  const float power_scale = -1.0f;

  encodeFloat32(data.current_L1, buffer + 3, offset); offset += 4;
  encodeFloat32(data.current_L2, buffer + 3, offset); offset += 4;
  encodeFloat32(data.current_L3, buffer + 3, offset); offset += 4;

  encodeFloat32(data.voltage_LN_avg, buffer + 3, offset); offset += 4;
  encodeFloat32(data.voltage_L1N, buffer + 3, offset); offset += 4;
  encodeFloat32(data.voltage_L2N, buffer + 3, offset); offset += 4;
  encodeFloat32(data.voltage_L3N, buffer + 3, offset); offset += 4;

  encodeFloat32(data.voltage_LL_avg, buffer + 3, offset); offset += 4;
  encodeFloat32(data.voltage_L1L2, buffer + 3, offset); offset += 4;
  encodeFloat32(data.voltage_L2L3, buffer + 3, offset); offset += 4;
  encodeFloat32(data.voltage_L3L1, buffer + 3, offset); offset += 4;
  encodeFloat32(data.frequency, buffer + 3, offset); offset += 4;

  encodeFloat32(data.power_total * power_scale, buffer + 3, offset); offset += 4;
  encodeFloat32(data.power_L1 * power_scale, buffer + 3, offset); offset += 4;
  encodeFloat32(data.power_L2 * power_scale, buffer + 3, offset); offset += 4;
  encodeFloat32(data.power_L3 * power_scale, buffer + 3, offset); offset += 4;

  encodeFloat32(data.reactive_total, buffer + 3, offset); offset += 4;
  encodeFloat32(data.reactive_L1, buffer + 3, offset); offset += 4;
  encodeFloat32(data.reactive_L2, buffer + 3, offset); offset += 4;
  encodeFloat32(data.reactive_L3, buffer + 3, offset); offset += 4;

  encodeFloat32(data.apparent_total, buffer + 3, offset); offset += 4;
  encodeFloat32(data.apparent_L1, buffer + 3, offset); offset += 4;
  encodeFloat32(data.apparent_L2, buffer + 3, offset); offset += 4;
  encodeFloat32(data.apparent_L3, buffer + 3, offset); offset += 4;

  encodeFloat32(data.pf_total, buffer + 3, offset); offset += 4;
  encodeFloat32(data.pf_L1, buffer + 3, offset); offset += 4;
  encodeFloat32(data.pf_L2, buffer + 3, offset); offset += 4;
  encodeFloat32(data.pf_L3, buffer + 3, offset); offset += 4;

  encodeFloat32(data.demand_total * power_scale, buffer + 3, offset); offset += 4;
  encodeFloat32(data.demand_L1 * power_scale, buffer + 3, offset); offset += 4;
  encodeFloat32(data.demand_L2 * power_scale, buffer + 3, offset); offset += 4;
  encodeFloat32(data.demand_L3 * power_scale, buffer + 3, offset); offset += 4;

  encodeFloat32(data.import_total, buffer + 3, offset); offset += 4;
  encodeFloat32(data.import_L1, buffer + 3, offset); offset += 4;
  encodeFloat32(data.import_L2, buffer + 3, offset); offset += 4;
  encodeFloat32(data.import_L3, buffer + 3, offset); offset += 4;

  encodeFloat32(data.export_total, buffer + 3, offset); offset += 4;
  encodeFloat32(data.export_L1, buffer + 3, offset); offset += 4;
  encodeFloat32(data.export_L2, buffer + 3, offset); offset += 4;
  encodeFloat32(data.export_L3, buffer + 3, offset); offset += 4;

  uint16_t crc = 0xFFFF;
  for (int i = 0; i < 163; i++) {
    uint8_t b = buffer[i];
    crc ^= (uint16_t)b;
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  buffer[163] = crc & 0xFF;
  buffer[164] = (crc >> 8) & 0xFF;

  return true;
}

static inline uint16_t crc16_modbus(const uint8_t* p, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= p[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

void printHexDump(const char* label, const uint8_t* buf, size_t len) {
  Serial.printf("   %s [%zu]: ", label, len);
  for (size_t i = 0; i < len; i += 16) {
    Serial.printf("\n      ");
    for (size_t j = 0; j < 16 && (i+j) < len; ++j) Serial.printf("%02X ", buf[i+j]);
  }
  Serial.println();
}

bool applyPowerCorrection(uint8_t* raw, uint16_t len, float correction) {
  if (!raw || len < 165) return false;

  uint8_t* payload = raw + 3;
  union { uint32_t u; float f; } converter;

  // 1. Correct phase powers (distribute wallbox power evenly across 3 phases)
  float wallboxPowerPerPhase = correction / 3.0f;

  // Power L1 at offset 13*4 = 52
  uint8_t* powerL1Bytes = payload + 52;
  converter.u = (uint32_t(powerL1Bytes[0]) << 24) | (uint32_t(powerL1Bytes[1]) << 16) |
                (uint32_t(powerL1Bytes[2]) << 8)  | uint32_t(powerL1Bytes[3]);
  float correctedPowerL1 = converter.f + wallboxPowerPerPhase;
  converter.f = correctedPowerL1;
  powerL1Bytes[0] = (converter.u >> 24) & 0xFF;
  powerL1Bytes[1] = (converter.u >> 16) & 0xFF;
  powerL1Bytes[2] = (converter.u >> 8)  & 0xFF;
  powerL1Bytes[3] = converter.u         & 0xFF;

  // Power L2 at offset 14*4 = 56
  uint8_t* powerL2Bytes = payload + 56;
  converter.u = (uint32_t(powerL2Bytes[0]) << 24) | (uint32_t(powerL2Bytes[1]) << 16) |
                (uint32_t(powerL2Bytes[2]) << 8)  | uint32_t(powerL2Bytes[3]);
  float correctedPowerL2 = converter.f + wallboxPowerPerPhase;
  converter.f = correctedPowerL2;
  powerL2Bytes[0] = (converter.u >> 24) & 0xFF;
  powerL2Bytes[1] = (converter.u >> 16) & 0xFF;
  powerL2Bytes[2] = (converter.u >> 8)  & 0xFF;
  powerL2Bytes[3] = converter.u         & 0xFF;

  // Power L3 at offset 15*4 = 60
  uint8_t* powerL3Bytes = payload + 60;
  converter.u = (uint32_t(powerL3Bytes[0]) << 24) | (uint32_t(powerL3Bytes[1]) << 16) |
                (uint32_t(powerL3Bytes[2]) << 8)  | uint32_t(powerL3Bytes[3]);
  float correctedPowerL3 = converter.f + wallboxPowerPerPhase;
  converter.f = correctedPowerL3;
  powerL3Bytes[0] = (converter.u >> 24) & 0xFF;
  powerL3Bytes[1] = (converter.u >> 16) & 0xFF;
  powerL3Bytes[2] = (converter.u >> 8)  & 0xFF;
  powerL3Bytes[3] = converter.u         & 0xFF;

  // 2. Correct total power (add wallbox power)
  size_t totalPowerOffset = 12 * 4;
  uint8_t* powerBytes = payload + totalPowerOffset;
  converter.u = (uint32_t(powerBytes[0]) << 24) | (uint32_t(powerBytes[1]) << 16) |
                (uint32_t(powerBytes[2]) << 8)  | uint32_t(powerBytes[3]);

  float originalPower = converter.f;
  float correctedPower = originalPower + correction;  // ADD wallbox power (back to addition)
  converter.f = correctedPower;

  powerBytes[0] = (converter.u >> 24) & 0xFF;
  powerBytes[1] = (converter.u >> 16) & 0xFF;
  powerBytes[2] = (converter.u >> 8)  & 0xFF;
  powerBytes[3] = converter.u         & 0xFF;

  // 3. Correct phase demands (distribute wallbox power evenly across 3 phases)
  // Demand L1 at offset 29*4 = 116
  uint8_t* demandL1Bytes = payload + 116;
  converter.u = (uint32_t(demandL1Bytes[0]) << 24) | (uint32_t(demandL1Bytes[1]) << 16) |
                (uint32_t(demandL1Bytes[2]) << 8)  | uint32_t(demandL1Bytes[3]);
  float correctedDemandL1 = converter.f + wallboxPowerPerPhase;
  converter.f = correctedDemandL1;
  demandL1Bytes[0] = (converter.u >> 24) & 0xFF;
  demandL1Bytes[1] = (converter.u >> 16) & 0xFF;
  demandL1Bytes[2] = (converter.u >> 8)  & 0xFF;
  demandL1Bytes[3] = converter.u         & 0xFF;

  // Demand L2 at offset 30*4 = 120
  uint8_t* demandL2Bytes = payload + 120;
  converter.u = (uint32_t(demandL2Bytes[0]) << 24) | (uint32_t(demandL2Bytes[1]) << 16) |
                (uint32_t(demandL2Bytes[2]) << 8)  | uint32_t(demandL2Bytes[3]);
  float correctedDemandL2 = converter.f + wallboxPowerPerPhase;
  converter.f = correctedDemandL2;
  demandL2Bytes[0] = (converter.u >> 24) & 0xFF;
  demandL2Bytes[1] = (converter.u >> 16) & 0xFF;
  demandL2Bytes[2] = (converter.u >> 8)  & 0xFF;
  demandL2Bytes[3] = converter.u         & 0xFF;

  // Demand L3 at offset 31*4 = 124
  uint8_t* demandL3Bytes = payload + 124;
  converter.u = (uint32_t(demandL3Bytes[0]) << 24) | (uint32_t(demandL3Bytes[1]) << 16) |
                (uint32_t(demandL3Bytes[2]) << 8)  | uint32_t(demandL3Bytes[3]);
  float correctedDemandL3 = converter.f + wallboxPowerPerPhase;
  converter.f = correctedDemandL3;
  demandL3Bytes[0] = (converter.u >> 24) & 0xFF;
  demandL3Bytes[1] = (converter.u >> 16) & 0xFF;
  demandL3Bytes[2] = (converter.u >> 8)  & 0xFF;
  demandL3Bytes[3] = converter.u         & 0xFF;

  // 4. Correct total demand (add wallbox power)
  size_t demandTotalOffset = 28 * 4;
  uint8_t* demandBytes = payload + demandTotalOffset;
  converter.u = (uint32_t(demandBytes[0]) << 24) | (uint32_t(demandBytes[1]) << 16) |
                (uint32_t(demandBytes[2]) << 8)  | uint32_t(demandBytes[3]);

  float originalDemand = converter.f;
  float correctedDemand = originalDemand + correction;  // ADD wallbox power (back to addition)
  converter.f = correctedDemand;

  demandBytes[0] = (converter.u >> 24) & 0xFF;
  demandBytes[1] = (converter.u >> 16) & 0xFF;
  demandBytes[2] = (converter.u >> 8)  & 0xFF;
  demandBytes[3] = converter.u         & 0xFF;

  // 5. Recalculate CRC after all modifications
  uint16_t newCrc = ModbusRTU485::crc16(raw, len - 2);
  raw[len - 2] = newCrc & 0xFF;
  raw[len - 1] = (newCrc >> 8) & 0xFF;

  return true;
}

bool parseDTSU666Response(const uint8_t* raw, uint16_t len, DTSU666Data& data) {
  if (!raw || len < 165) return false;

  const uint8_t* payload = raw + 3;

  auto parseFloat = [&](size_t offset) -> float {
    const uint8_t* bytes = payload + offset;
    uint32_t bits = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                    (uint32_t(bytes[2]) << 8)  | uint32_t(bytes[3]);
    union { uint32_t u; float f; } converter;
    converter.u = bits;
    return converter.f;
  };

  data.current_L1 = parseFloat(0);
  data.current_L2 = parseFloat(4);
  data.current_L3 = parseFloat(8);

  data.voltage_LN_avg = parseFloat(12);
  data.voltage_L1N = parseFloat(16);
  data.voltage_L2N = parseFloat(20);
  data.voltage_L3N = parseFloat(24);

  data.voltage_LL_avg = parseFloat(28);
  data.voltage_L1L2 = parseFloat(32);
  data.voltage_L2L3 = parseFloat(36);
  data.voltage_L3L1 = parseFloat(40);
  data.frequency = parseFloat(44);

  data.power_total = parseFloat(48);
  data.power_L1 = parseFloat(52);
  data.power_L2 = parseFloat(56);
  data.power_L3 = parseFloat(60);

  return true;
}