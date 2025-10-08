#pragma once

#include <Arduino.h>
#include "config.h"
#include "ModbusRTU485.h"

// DTSU-666 parsed data structure — 40 FP32 values (80 registers: 2102–2181)
struct DTSU666Data {
  // 2102–2106: Currents (A)
  float current_L1, current_L2, current_L3;

  // 2108–2114: Line-to-neutral voltages (V)
  float voltage_LN_avg, voltage_L1N, voltage_L2N, voltage_L3N;

  // 2116–2124: Line-to-line voltages (V) and frequency (Hz)
  float voltage_LL_avg, voltage_L1L2, voltage_L2L3, voltage_L3L1;
  float frequency;

  // 2126–2132: Active power (W) — inverted in meter to simulate production
  float power_total, power_L1, power_L2, power_L3;

  // 2134–2140: Reactive power (var)
  float reactive_total, reactive_L1, reactive_L2, reactive_L3;

  // 2142–2148: Apparent power (VA)
  float apparent_total, apparent_L1, apparent_L2, apparent_L3;

  // 2150–2156: Power factor (0..1)
  float pf_total, pf_L1, pf_L2, pf_L3;

  // 2158–2164: Active power demand (W) — inverted in meter
  float demand_total, demand_L1, demand_L2, demand_L3;

  // 2166–2172: Import energy (kWh)
  float import_total, import_L1, import_L2, import_L3;

  // 2174–2180: Export energy (kWh)
  float export_total, export_L1, export_L2, export_L3;
};

// Optional: U_WORD metadata block (read via separate 0x03 requests)
struct DTSU666Meta {
  uint16_t status = 0;           // 2001
  uint16_t version = 0;          // 2214
  uint16_t passcode = 0;         // 2215
  uint16_t zero_clear_flag = 0;  // 2216
  uint16_t connection_mode = 0;  // 2217
  uint16_t irat = 0;             // 2218
  uint16_t urat = 0;             // 2219
  uint16_t protocol = 0;         // 2220
  uint16_t address = 0;          // 2221
  uint16_t baud = 0;             // 2222
  uint16_t meter_type = 0;       // 2223
};

// Track last MODBUS request to associate replies for decoding
struct LastRequestInfo {
  bool     valid = false;
  uint8_t  id = 0;
  uint8_t  fc = 0;
  uint16_t startAddr = 0;
  uint16_t qty = 0;
  uint32_t ts = 0;
};

// Thread-safe shared DTSU data structure for dual-task architecture
struct SharedDTSUData {
  SemaphoreHandle_t mutex;              // Mutex for thread-safe access
  bool valid;                           // Whether data is valid
  uint32_t timestamp;                   // When data was last updated (millis)
  uint8_t responseBuffer[165];          // Latest DTSU response with corrections applied
  uint16_t responseLength;              // Length of response
  DTSU666Data parsedData;               // Parsed data for power corrections
  uint32_t updateCount;                 // Number of successful updates
};

// Function declarations
bool parseDTSU666Data(uint16_t startAddr, const ModbusMessage& msg, DTSU666Data& data);
bool parseDTSU666MetaWords(uint16_t startAddr, const ModbusMessage& msg, DTSU666Meta& meta);
bool encodeDTSU666Response(const DTSU666Data& data, uint8_t* buffer, size_t bufferSize);
bool applyPowerCorrection(uint8_t* raw, uint16_t len, float correction);
bool parseDTSU666Response(const uint8_t* raw, uint16_t len, DTSU666Data& data);

// Utility functions
float parseFloat32(const uint8_t* data, size_t offset);
void encodeFloat32(float value, uint8_t* data, size_t offset);
int16_t parseInt16(const uint8_t* data, size_t offset);
uint16_t parseUInt16(const uint8_t* data, size_t offset);

// Debug functions
void debugDTSUData(const DTSU666Data& data);
void debugMQTTData(const String& time, const String& smid, const DTSU666Data& data);
void printHexDump(const char* label, const uint8_t* buf, size_t len);

// Constants
extern const char* dtsuRegisterNames[40];