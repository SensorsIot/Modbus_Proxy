//
// ESP32 MODBUS RTU Intelligent Proxy with Power Correction
// ========================================================
//
// ## Purpose
// Intelligent proxy between SUN2000 solar inverter and DTSU-666 energy meter that provides
// real-time power measurement correction by subtracting wallbox charging power from DTSU readings
// to ensure optimal solar generation control excluding wallbox consumption.
//
// ## System Architecture
//
// ### Hardware Components
// - **ESP32**: Dual-core microcontroller with WiFi
// - **Dual RS-485 Interfaces**:
//   - UART2 (Pins 16 RX, 17 TX): SUN2000 inverter communication
//   - UART1 (Pins 18 RX, 19 TX): DTSU-666 energy meter communication
// - **WiFi**: MQTT communication with reference meter
//
// ### Software Components
// - **Simple Direct Proxy**: `proxyTask()` handles main MODBUS communication (Core 1, Priority 2)
// - **MQTT Task**: `mqttTask()` manages WiFi/MQTT and EVCC API polling (Core 0, Priority 1)
// - **Watchdog Task**: `watchdogTask()` independent system health monitoring (Core 0, Priority 3)
// - **ModbusRTU485 Library**: Custom library for MODBUS RTU protocol handling
// - **Power Correction Engine**: Real-time IEEE 754 float modification
// - **Auto-Restart System**: Comprehensive failure detection and recovery
//
// ## Communication Flow
//
// ```
// ┌─────────┐    MODBUS RTU     ┌─────────┐    MODBUS RTU     ┌─────────┐
// │ SUN2000 │◄─────────────────►│  ESP32  │◄─────────────────►│DTSU-666 │
// │Inverter │   9600 baud 8N1   │  Proxy  │   9600 baud 8N1   │ Meter   │
// └─────────┘                   └─────────┘                   └─────────┘
//                                    │
//                                    │ WiFi/HTTP
//                                    │ EVCC API + MQTT
//                                    ▼
//                               ┌─────────┐
//                               │ EVCC    │
//                               │ Wallbox │
//                               │HTTP API │
//                               └─────────┘
// ```
//
// ## Power Correction Algorithm
//
// **Objective**: Subtract wallbox charging power from DTSU-666 readings to exclude wallbox consumption
//
// **Process**:
// 1. **EVCC API Polling**: HTTP GET every 10 seconds to EVCC API for wallbox power
// 2. **Thread-Safe Storage**: Store chargePower in mutex-protected shared structure
// 3. **Threshold Check**: If `|P_wallbox| > 1000W` → wallbox charging detected
// 4. **Power Correction Application**: Add wallbox power to DTSU-666 power values
// 5. **Proportional Distribution**: Distribute correction across L1/L2/L3 phases
// 6. **Persistent Correction**: Maintain correction until next API update
//
// **Benefits**:
// - SUN2000 sees household consumption excluding wallbox charging
// - Allows separate control of solar generation and wallbox charging
// - Prevents wallbox charging from triggering unnecessary solar production
//
// ## Data Processing
//
// ### DTSU-666 Data Format
// - **Registers**: 2102-2181 (80 registers = 40 IEEE 754 floats)
// - **Payload Size**: 160 bytes of measurement data
// - **Float Format**: Big-endian IEEE 754 32-bit
// - **Power Sign**: Negative = importing, Positive = exporting (DTSU controls)
//
// ### EVCC API Data Format
// - **URL**: `http://192.168.0.202:7070/api/state` - EVCC system state
// - **Format**: JSON with loadpoints[0].chargePower field
// - **Power Value**: Direct wallbox charging power (positive value when charging)
// - **Polling Frequency**: Every 10 seconds for system efficiency
//
// ## Message Processing Pipeline
//
// ```
// 1. SUN2000 Request     → modbusSUN.read()
// 2. Forward to DTSU     → SerialDTU.write()
// 3. DTSU Response       → modbusDTU.read()
// 4. Parse IEEE754       → parseDTSU666Reply()
// 5. Apply Corrections   → applyPowerCorrections()
// 6. Re-encode Data      → encodeDTSU666Reply()
// 7. CRC Recalculation   → ModbusRTU485::crc16()
// 8. Forward to SUN2000  → modbusSUN.write()
// ```
//
// ## Key Technical Details
//
// - **MODBUS Slave ID**: 11 (DTSU-666)
// - **Function Codes**: 0x03 (Read Holding), 0x04 (Read Input)
// - **Correction Threshold**: 1000W minimum wallbox power
// - **Phase Distribution**: Proportional based on existing loads
// - **Error Handling**: Graceful degradation when EVCC API unavailable
// - **Thread Safety**: Mutex-protected shared data structures
// - **Production Logging**: Clean 3-line output (DTSU, API, SUN2000)
//
// ## Auto-Restart & Health Monitoring System
//
// **Architecture**:
// - **Independent Watchdog Task**: Runs on Core 0 with highest priority (3)
// - **Task Heartbeats**: Each task updates its "last seen" timestamp
// - **Health Checks**: Every 5 seconds, comprehensive system assessment
// - **MQTT Error Reporting**: Real-time error notifications via MQTT
//
// **Failure Thresholds**:
// - **Task Watchdog**: 60 seconds without heartbeat → System restart
// - **EVCC API**: 20 consecutive failures → System restart
// - **MQTT**: 50 consecutive failures → System restart
// - **MODBUS**: 10 consecutive failures → System restart
// - **Memory**: < 20KB free heap → System restart
//
// **MQTT Error Topics**:
// - **`MBUS-PROXY/ERROR`**: Individual error events with context
// - **`MBUS-PROXY/HEALTH`**: Regular health status reports every 30 seconds
//
// **System Recovery**:
// - **Graceful Shutdown**: 5-second delay for MQTT error transmission
// - **ESP.restart()**: Complete system reset for fault recovery
// - **Restart Counter**: Tracks system restart frequency for debugging
//

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ModbusRTU485.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "credentials.h"

// Output options
// Use a macro so it is always available for conditional blocks and expressions.
#ifndef PRINT_FANCY_TABLE
#define PRINT_FANCY_TABLE 1  // Set to 1 to enable box-drawn comparison table
#endif

// DTSU-666 register definitions
#define DTSU_STATUS_REG         2001
#define DTSU_CURRENT_L1_REG     2102
#define DTSU_CURRENT_L2_REG     2104
#define DTSU_CURRENT_L3_REG     2106
#define DTSU_VOLTAGE_LN_AVG_REG 2108
#define DTSU_VOLTAGE_L1N_REG    2110
#define DTSU_VOLTAGE_L2N_REG    2112
#define DTSU_VOLTAGE_L3N_REG    2114
#define DTSU_VOLTAGE_LL_AVG_REG 2116
#define DTSU_VOLTAGE_L1L2_REG   2118
#define DTSU_VOLTAGE_L2L3_REG   2120
#define DTSU_VOLTAGE_L3L1_REG   2122
#define DTSU_FREQUENCY_REG      2124
#define DTSU_POWER_TOTAL_REG    2126
#define DTSU_POWER_L1_REG       2128
#define DTSU_POWER_L2_REG       2130
#define DTSU_POWER_L3_REG       2132
#define DTSU_REACTIVE_TOTAL_REG 2134
#define DTSU_REACTIVE_L1_REG    2136
#define DTSU_REACTIVE_L2_REG    2138
#define DTSU_REACTIVE_L3_REG    2140
#define DTSU_APPARENT_TOTAL_REG 2142
#define DTSU_APPARENT_L1_REG    2144
#define DTSU_APPARENT_L2_REG    2146
#define DTSU_APPARENT_L3_REG    2148
#define DTSU_PF_TOTAL_REG       2150
#define DTSU_PF_L1_REG          2152
#define DTSU_PF_L2_REG          2154
#define DTSU_PF_L3_REG          2156
#define DTSU_DEMAND_TOTAL_REG   2158
#define DTSU_DEMAND_L1_REG      2160
#define DTSU_DEMAND_L2_REG      2162
#define DTSU_DEMAND_L3_REG      2164
#define DTSU_IMPORT_TOTAL_REG   2166
#define DTSU_IMPORT_L1_REG      2168
#define DTSU_IMPORT_L2_REG      2170
#define DTSU_IMPORT_L3_REG      2172
#define DTSU_EXPORT_TOTAL_REG   2174
#define DTSU_EXPORT_L1_REG      2176
#define DTSU_EXPORT_L2_REG      2178
#define DTSU_EXPORT_L3_REG      2180
#define DTSU_VERSION_REG        2214
// Additional U_WORD configuration/status registers
#define DTSU_PASSCODE_REG             2215
#define DTSU_ZERO_CLEAR_FLAG_REG      2216
#define DTSU_CONNECTION_MODE_REG      2217
#define DTSU_CT_RATIO_REG             2218
#define DTSU_VT_RATIO_REG             2219
#define DTSU_PROTOCOL_SWITCH_REG      2220
#define DTSU_COMM_ADDRESS_REG         2221
#define DTSU_BAUD_REG                 2222
#define DTSU_METER_TYPE_REG           2223

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

// MQTT MBUS/SENSOR data structure
struct MQTTSensorData {
  String time;
  String smid;
  // Active power (kW)
  float pi, po;           // Total import/export power
  float pi1, pi2, pi3;    // Phase import power (W)
  float po1, po2, po3;    // Phase export power (W)
  // Voltage (V)
  float u1, u2, u3;       // Phase voltages
  // Current (A)
  float i1, i2, i3;       // Phase currents
  // Frequency (Hz)
  float f;                // Grid frequency
  // Energy (kWh)
  float ei, eo;           // Total import/export energy
  float ei1, ei2;         // Import energy tariff 1/2
  float eo1, eo2;         // Export energy tariff 1/2
  // Reactive energy (kVArh)
  float q5, q6, q7, q8;   // Quadrant reactive energy
  float q51, q52, q61, q62, q71, q72, q81, q82; // Tariff reactive energy
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

static LastRequestInfo g_lastReq;

// Pin definitions
#define RS485_SUN2000_RX_PIN 16
#define RS485_SUN2000_TX_PIN 17
#define RS485_DTU_RX_PIN 18
#define RS485_DTU_TX_PIN 19

// Baud rate for MODBUS communication
#define MODBUS_BAUDRATE 9600

// Define serial ports for communication
HardwareSerial SerialSUN(2);
HardwareSerial SerialDTU(1);  // DTSU-666 interface

// ModbusRTU485 instances
ModbusRTU485 modbusSUN;  // SUN2000 interface
ModbusRTU485 modbusDTU;  // DTSU-666 interface

// Global DTSU-666 data storage
DTSU666Data dtsu666Data;

// Buffer for re-encoding DTSU-666 messages
uint8_t reencodedBuffer[165];

// Thread-safe shared MQTT received data structure
struct SharedMQTTReceivedData {
  SemaphoreHandle_t semaphore;          // Semaphore for thread-safe access
  MQTTSensorData data;                  // The MQTT sensor data
  uint32_t timestamp;                   // When data was last updated
  bool dataValid;                       // Whether data is valid
  bool newPackageArrived;               // Flag for new package arrival
};
SharedMQTTReceivedData sharedMQTTRx = {NULL, {}, 0, false, false};

// Temporary global variables (for gradual migration)
MQTTSensorData mqttSensorData;
bool mqttDataReceived = false;
bool newMqttPackageArrived = false;

// Power correction variables
float powerCorrection = 0.0f;          // Watts to add to DTSU-666 power values (wallbox between DTSU and grid)
bool powerCorrectionActive = false;    // Whether correction is being applied
uint32_t lastCorrectionTime = 0;       // Timestamp of last correction calculation
const float CORRECTION_THRESHOLD = 1000.0f; // Minimum wallbox power for correction (W)

// EVCC HTTP API polling variables
const char* evccApiUrl = "http://192.168.0.202:7070/api/state";
const uint32_t HTTP_POLL_INTERVAL = 10000;  // Poll every 10 seconds
uint32_t lastHttpPoll = 0;               // Last time we polled the HTTP API

// Thread-safe shared EVCC data structure
struct SharedEVCCData {
  SemaphoreHandle_t mutex;              // Mutex for thread-safe access
  float chargePower;                    // Charge power from EVCC API (W)
  uint32_t timestamp;                   // When data was last updated (millis)
  bool valid;                           // Whether data is valid
  uint32_t updateCount;                 // Number of successful API calls
  uint32_t errorCount;                  // Number of failed API calls
};

SharedEVCCData sharedEVCC = {NULL, 0.0f, 0, false, 0, 0};
const uint32_t EVCC_DATA_MAX_AGE_MS = 10000; // 10 seconds max age for EVCC data

// Thread-safe shared DTSU data structure for dual-task architecture
struct SharedDTSUData {
  SemaphoreHandle_t mutex;              // Mutex for thread-safe access
  bool valid;                           // Whether data is valid
  uint32_t timestamp;                   // When data was last updated (millis)
  uint8_t responseBuffer[165];          // Latest DTSU response with corrections applied
  uint16_t responseLength;              // Length of response
  DTSU666Data parsedData;               // Parsed data for power corrections
  uint32_t updateCount;                 // Number of successful updates
  uint32_t errorCount;                  // Number of polling errors
  uint32_t serveCount;                  // Number of times served to SUN2000
};

SharedDTSUData sharedDTSU = {NULL, false, 0, {}, 0, {}, 0, 0, 0};

// Thread-safe EVCC data access functions
bool getEVCCChargePower(float& chargePower, uint32_t& dataAge) {
  if (xSemaphoreTake(sharedEVCC.mutex, 5 / portTICK_PERIOD_MS) == pdTRUE) {
    if (!sharedEVCC.valid) {
      xSemaphoreGive(sharedEVCC.mutex);
      return false;
    }

    // Check data age
    dataAge = millis() - sharedEVCC.timestamp;
    if (dataAge > EVCC_DATA_MAX_AGE_MS) {
      xSemaphoreGive(sharedEVCC.mutex);
      return false; // Data too old
    }

    chargePower = sharedEVCC.chargePower;
    xSemaphoreGive(sharedEVCC.mutex);
    return true;
  }
  return false; // Could not acquire mutex
}

void updateEVCCChargePower(float chargePower, bool valid) {
  if (xSemaphoreTake(sharedEVCC.mutex, 100 / portTICK_PERIOD_MS) == pdTRUE) {
    sharedEVCC.chargePower = chargePower;
    sharedEVCC.timestamp = millis();
    sharedEVCC.valid = valid;

    if (valid) {
      sharedEVCC.updateCount++;
    } else {
      sharedEVCC.errorCount++;
    }

    xSemaphoreGive(sharedEVCC.mutex);
  }
}

// MQTT publish queue item structure
struct MQTTPublishItem {
  DTSU666Data correctedData;            // Corrected power data to publish
  DTSU666Data originalData;             // Original DTSU data (before correction)
  bool correctionApplied;               // Whether correction was applied
  float correctionValue;                // The correction value applied
  uint32_t timestamp;                   // When data was generated
};

// MQTT publish queue
QueueHandle_t mqttPublishQueue = NULL;
const uint32_t DTSU_POLL_INTERVAL_MS = 1500;  // Poll DTSU every 1.5 seconds
const uint32_t DATA_VALIDITY_MS = 5000;       // Data considered stale after 5 seconds

// Auto-restart and watchdog system
const uint32_t WATCHDOG_TIMEOUT_MS = 60000;      // 1 minute watchdog timeout
const uint32_t EVCC_FAILURE_THRESHOLD = 20;      // Max consecutive EVCC API failures before restart
const uint32_t MQTT_FAILURE_THRESHOLD = 50;      // Max MQTT reconnect attempts before restart
const uint32_t MODBUS_FAILURE_THRESHOLD = 10;    // Max consecutive MODBUS failures before restart
const uint32_t MIN_FREE_HEAP = 20000;            // Minimum free heap before restart (20KB)
const uint32_t HEALTH_CHECK_INTERVAL_MS = 30000; // Health check every 30 seconds

// System health monitoring structure
struct SystemHealth {
  uint32_t proxyTaskLastSeen;
  uint32_t mqttTaskLastSeen;
  uint32_t evccConsecutiveFailures;
  uint32_t mqttConsecutiveFailures;
  uint32_t modbusConsecutiveFailures;
  uint32_t totalRestarts;
  uint32_t lastHealthCheck;
  bool systemHealthy;
};

SystemHealth systemHealth = {0};

// WiFi and MQTT client objects
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// DTSU-666 parsing functions
int16_t parseInt16(const uint8_t* data, size_t offset) {
  // MODBUS RTU uses Big-Endian 16-bit words
  return (int16_t)((data[offset] << 8) | data[offset + 1]);
}

uint16_t parseUInt16(const uint8_t* data, size_t offset) {
  // MODBUS RTU uses Big-Endian 16-bit words  
  return (uint16_t)((data[offset] << 8) | data[offset + 1]);
}

// Define byte order options for testing different DTSU-666 float formats
#define DTSU_BYTE_ORDER_ABCD 1   // Big Endian: A B C D (Most common)
#define DTSU_BYTE_ORDER_DCBA 2   // Little Endian: D C B A  
#define DTSU_BYTE_ORDER_BADC 3   // Mid-Big Endian: B A D C
#define DTSU_BYTE_ORDER_CDAB 4   // Mid-Little Endian: C D A B

// Current byte order setting - change this to test different formats
#define DTSU_CURRENT_ORDER DTSU_BYTE_ORDER_ABCD

float parseFloat32(const uint8_t* data, size_t offset) {
  union {
    uint32_t u;
    float f;
  } converter;
  
#if DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_ABCD
  // Big Endian (ABCD): Standard network byte order
  converter.u = ((uint32_t)data[offset]     << 24) |
                ((uint32_t)data[offset + 1] << 16) |
                ((uint32_t)data[offset + 2] << 8)  |
                ((uint32_t)data[offset + 3]);
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_DCBA
  // Little Endian (DCBA): Reverse byte order
  converter.u = ((uint32_t)data[offset + 3] << 24) |
                ((uint32_t)data[offset + 2] << 16) |
                ((uint32_t)data[offset + 1] << 8)  |
                ((uint32_t)data[offset]);
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_BADC
  // Mid-Big Endian (BADC): Word-swapped big endian
  converter.u = ((uint32_t)data[offset + 1] << 24) |
                ((uint32_t)data[offset]     << 16) |
                ((uint32_t)data[offset + 3] << 8)  |
                ((uint32_t)data[offset + 2]);
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_CDAB
  // Mid-Little Endian (CDAB): MODBUS word-swapped format
  converter.u = ((uint32_t)data[offset + 2] << 24) |
                ((uint32_t)data[offset + 3] << 16) |
                ((uint32_t)data[offset]     << 8)  |
                ((uint32_t)data[offset + 1]);
#endif
  
  return converter.f;
}

void encodeFloat32(uint8_t* data, size_t offset, float value) {
  union {
    uint32_t u;
    float f;
  } converter;
  
  converter.f = value;

#if DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_ABCD
  // Big Endian (ABCD)
  data[offset]     = (converter.u >> 24) & 0xFF;
  data[offset + 1] = (converter.u >> 16) & 0xFF;
  data[offset + 2] = (converter.u >> 8)  & 0xFF;
  data[offset + 3] = converter.u         & 0xFF;
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_DCBA
  // Little Endian (DCBA)
  data[offset]     = converter.u         & 0xFF;
  data[offset + 1] = (converter.u >> 8)  & 0xFF;
  data[offset + 2] = (converter.u >> 16) & 0xFF;
  data[offset + 3] = (converter.u >> 24) & 0xFF;
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_BADC
  // Mid-Big Endian (BADC)
  data[offset]     = (converter.u >> 16) & 0xFF;
  data[offset + 1] = (converter.u >> 24) & 0xFF;
  data[offset + 2] = converter.u         & 0xFF;
  data[offset + 3] = (converter.u >> 8)  & 0xFF;
#elif DTSU_CURRENT_ORDER == DTSU_BYTE_ORDER_CDAB
  // Mid-Little Endian (CDAB)
  data[offset]     = (converter.u >> 8)  & 0xFF;
  data[offset + 1] = converter.u         & 0xFF;
  data[offset + 2] = (converter.u >> 24) & 0xFF;
  data[offset + 3] = (converter.u >> 16) & 0xFF;
#endif
}

bool parseDTSU666Reply(const ModbusMessage& msg, DTSU666Data& data) {
  if (!msg.valid || msg.type != MBType::Reply || !msg.raw) {
    return false;
  }

  if (msg.fc != 0x03 && msg.fc != 0x04) {
    return false;
  }

  const uint8_t* payload = &msg.raw[3];
  size_t payloadSize = msg.byteCount;

  // Check if this looks like a DTSU-666 reply (160 bytes of data for 80 registers starting at 2102)
  if (payloadSize != 160) {
    return false;
  }

  // DTSU-666 scaling formulas from manual:
  // Voltage: U = URMS × (UrAt × 0.1) × 0.1
  // Current: I = IRMS × IrAt × 0.001
  // Power: P = P × (UrAt × 0.1) × IrAt × 0.1
  // Power Factor: PF = PF × 0.001
  // Frequency: F = Freq × 0.01
  // Energy: Ep = E × UrAt × IrAt
  //
  // Based on your log showing reasonable 237V, 4928W values, assuming UrAt=10, IrAt=1:
  // Voltage: U = URMS × (10 × 0.1) × 0.1 = URMS × 1.0 × 0.1 = URMS × 0.1
  // Current: I = IRMS × 1 × 0.001 = IRMS × 0.001
  // Power: P = P × (10 × 0.1) × 1 × 0.1 = P × 1.0 × 0.1 = P × 0.1

  const float volt_scale = 1.0f;       // Voltage scaling - keep as raw (values already correct)
  const float current_scale = 1.0f;    // Engineering units
  const float power_scale = 1.0f;      // Power scaling - keep as raw (values already correct)
  const float pf_scale = 1.0f;         // Engineering units
  const float freq_scale = 1.0f;       // Engineering units
  const float energy_scale = 1.0f;     // Energy scaling - keep as raw for now

  // Parse all 80 registers sequentially (160 bytes total)
  // Each register = 2 bytes, 2 registers = 4 bytes = 1 FP32 float
  int offset = 0;
  
  // Registers 2102-2106: Current measurements (A) - FP32
  data.current_L1 = parseFloat32(payload, offset) * current_scale; offset += 4;
  data.current_L2 = parseFloat32(payload, offset) * current_scale; offset += 4;
  data.current_L3 = parseFloat32(payload, offset) * current_scale; offset += 4;
  
  // Registers 2108-2115: Line-to-neutral voltages only (per manual)
  data.voltage_LN_avg = parseFloat32(payload, offset) * volt_scale; offset += 4;  // 2108: Average L-N voltage
  data.voltage_L1N = parseFloat32(payload, offset) * volt_scale; offset += 4;     // 2110: L1-N voltage
  data.voltage_L2N = parseFloat32(payload, offset) * volt_scale; offset += 4;     // 2112: L2-N voltage
  data.voltage_L3N = parseFloat32(payload, offset) * volt_scale; offset += 4;     // 2114: L3-N voltage
  
  // Registers 2116-2124: Line-to-line voltages and frequency (per manual)
  data.voltage_LL_avg = parseFloat32(payload, offset) * volt_scale; offset += 4;  // 2116: Average L-L voltage
  data.voltage_L1L2 = parseFloat32(payload, offset) * volt_scale; offset += 4;    // 2118: L1-L2 voltage
  data.voltage_L2L3 = parseFloat32(payload, offset) * volt_scale; offset += 4;    // 2120: L2-L3 voltage
  data.voltage_L3L1 = parseFloat32(payload, offset) * volt_scale; offset += 4;    // 2122: L3-L1 voltage
  data.frequency = parseFloat32(payload, offset) * freq_scale; offset += 4;       // 2124: Grid frequency
  
  // Registers 2126-2132: Active power (W) - FP32 (invert sign from meter)
  float raw_power_total = parseFloat32(payload, offset) * power_scale; offset += 4;
  float raw_power_L1 = parseFloat32(payload, offset) * power_scale; offset += 4;
  float raw_power_L2 = parseFloat32(payload, offset) * power_scale; offset += 4;
  float raw_power_L3 = parseFloat32(payload, offset) * power_scale; offset += 4;

  // Apply sign inversion
  data.power_total = -raw_power_total;
  data.power_L1 = -raw_power_L1;
  data.power_L2 = -raw_power_L2;
  data.power_L3 = -raw_power_L3;

  // DEBUG: DTSU Power Parsing
  // Serial.printf("🔍 DEBUG DTSU RAW: P_total=%.1f, P_L1=%.1f, P_L2=%.1f, P_L3=%.1f\n",
  //              raw_power_total, raw_power_L1, raw_power_L2, raw_power_L3);
  // Serial.printf("🔍 DEBUG DTSU INVERTED: P_total=%.1f, P_L1=%.1f, P_L2=%.1f, P_L3=%.1f\n",
  //              data.power_total, data.power_L1, data.power_L2, data.power_L3);
  
  data.reactive_total = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.reactive_L1 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.reactive_L2 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.reactive_L3 = parseFloat32(payload, offset) * power_scale; offset += 4;
  
  data.apparent_total = parseFloat32(payload, offset) * power_scale; offset += 4; // Assuming apparent power has same scale
  data.apparent_L1 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.apparent_L2 = parseFloat32(payload, offset) * power_scale; offset += 4;
  data.apparent_L3 = parseFloat32(payload, offset) * power_scale; offset += 4;
  
  data.pf_total = parseFloat32(payload, offset) * pf_scale; offset += 4;
  data.pf_L1 = parseFloat32(payload, offset) * pf_scale; offset += 4;
  data.pf_L2 = parseFloat32(payload, offset) * pf_scale; offset += 4;
  data.pf_L3 = parseFloat32(payload, offset) * pf_scale; offset += 4;
  
  data.demand_total = -parseFloat32(payload, offset) * power_scale; offset += 4;  // Negate demand values
  data.demand_L1 = -parseFloat32(payload, offset) * power_scale; offset += 4;
  data.demand_L2 = -parseFloat32(payload, offset) * power_scale; offset += 4;
  data.demand_L3 = -parseFloat32(payload, offset) * power_scale; offset += 4;
  
  data.import_total = parseFloat32(payload, offset); offset += 4; // Energy scale factor seems more complex, skipping for now
  data.import_L1 = parseFloat32(payload, offset); offset += 4;
  data.import_L2 = parseFloat32(payload, offset); offset += 4;
  data.import_L3 = parseFloat32(payload, offset); offset += 4;
  
  data.export_total = parseFloat32(payload, offset); offset += 4;
  data.export_L1 = parseFloat32(payload, offset); offset += 4;
  data.export_L2 = parseFloat32(payload, offset); offset += 4;
  data.export_L3 = parseFloat32(payload, offset); offset += 4;
  
  // End of 40 FP32 values (80 registers)
  
  // Total offset should be 160 bytes (40 float32 values * 4 bytes each)

  return true;
}

// Helper: parse U_WORD replies for status and meta block.
// Caller must supply the starting address used in the request since replies do not include it.
static inline uint16_t be16u(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
bool parseDTSU666MetaWords(uint16_t startAddr, const ModbusMessage& msg, DTSU666Meta& meta) {
  if (!msg.valid || msg.type != MBType::Reply || !msg.raw) return false;
  if (msg.fc != 0x03 && msg.fc != 0x04) return false;
  const uint8_t* payload = &msg.raw[3];
  const size_t bc = msg.byteCount; // bytes

  bool parsed = false;

  if (startAddr == DTSU_STATUS_REG && bc >= 2) {
    meta.status = be16u(payload);
    parsed = true;
  }

  if (startAddr == DTSU_VERSION_REG && bc >= 20) { // 2214..2223 (10 registers)
    size_t o = 0;
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
    parsed = true;
  }

  return parsed;
}

// Apply power corrections to DTSU data before encoding
DTSU666Data applyPowerCorrections(const DTSU666Data& originalData) {
  DTSU666Data correctedData = originalData; // Copy original data

  if (powerCorrectionActive && powerCorrection != 0.0f) {
    // Apply corrections to power values (watts)
    correctedData.power_total += powerCorrection;
    correctedData.power_L1 += powerCorrection / 3.0f;  // Distribute correction across phases
    correctedData.power_L2 += powerCorrection / 3.0f;
    correctedData.power_L3 += powerCorrection / 3.0f;

    // Also apply to demand values (they typically mirror power)
    correctedData.demand_total += powerCorrection;
    correctedData.demand_L1 += powerCorrection / 3.0f;
    correctedData.demand_L2 += powerCorrection / 3.0f;
    correctedData.demand_L3 += powerCorrection / 3.0f;

    Serial.printf("⚡ Power correction: +%.0fW applied\n", powerCorrection);
  }

  return correctedData;
}

bool encodeDTSU666Reply(const DTSU666Data& data, uint8_t* buffer, size_t bufferSize) {
  // Check buffer size (need 165 bytes: ID + FC + ByteCount + 160 data bytes + CRC)
  if (bufferSize < 165) {
    return false;
  }
  
  // MODBUS header (will be filled by caller)
  buffer[0] = 0x0B;  // Slave ID 11
  buffer[1] = 0x03;  // Function Code
  buffer[2] = 0xA0;  // Byte count (160 bytes)
  
  // Encode data payload (160 bytes starting at offset 3) - matches parsing order exactly
  uint8_t* payload = &buffer[3];
  int offset = 0;
  
  // 2102-2106: Currents
  encodeFloat32(payload, offset, data.current_L1); offset += 4;
  encodeFloat32(payload, offset, data.current_L2); offset += 4;
  encodeFloat32(payload, offset, data.current_L3); offset += 4;
  
  // Registers 2108-2115: Line-to-neutral voltages
  encodeFloat32(payload, offset, data.voltage_LN_avg); offset += 4;
  encodeFloat32(payload, offset, data.voltage_L1N); offset += 4;
  encodeFloat32(payload, offset, data.voltage_L2N); offset += 4;
  encodeFloat32(payload, offset, data.voltage_L3N); offset += 4;

  // Registers 2116-2124: Line-to-line voltages and frequency
  encodeFloat32(payload, offset, data.voltage_LL_avg); offset += 4;
  encodeFloat32(payload, offset, data.voltage_L1L2); offset += 4;
  encodeFloat32(payload, offset, data.voltage_L2L3); offset += 4;
  encodeFloat32(payload, offset, data.voltage_L3L1); offset += 4;
  encodeFloat32(payload, offset, data.frequency); offset += 4;
  
  // 2126-2132: Active power — invert back for encoding
  encodeFloat32(payload, offset, -data.power_total); offset += 4;   
  encodeFloat32(payload, offset, -data.power_L1); offset += 4;
  encodeFloat32(payload, offset, -data.power_L2); offset += 4;
  encodeFloat32(payload, offset, -data.power_L3); offset += 4;
  
  encodeFloat32(payload, offset, data.reactive_total); offset += 4;
  encodeFloat32(payload, offset, data.reactive_L1); offset += 4;
  encodeFloat32(payload, offset, data.reactive_L2); offset += 4;
  encodeFloat32(payload, offset, data.reactive_L3); offset += 4;
  
  encodeFloat32(payload, offset, data.apparent_total); offset += 4;
  encodeFloat32(payload, offset, data.apparent_L1); offset += 4;
  encodeFloat32(payload, offset, data.apparent_L2); offset += 4;
  encodeFloat32(payload, offset, data.apparent_L3); offset += 4;
  
  encodeFloat32(payload, offset, data.pf_total); offset += 4;
  encodeFloat32(payload, offset, data.pf_L1); offset += 4;
  encodeFloat32(payload, offset, data.pf_L2); offset += 4;
  encodeFloat32(payload, offset, data.pf_L3); offset += 4;
  
  // 2158-2164: Active power demand — invert back for encoding
  encodeFloat32(payload, offset, -data.demand_total); offset += 4;
  encodeFloat32(payload, offset, -data.demand_L1); offset += 4;
  encodeFloat32(payload, offset, -data.demand_L2); offset += 4;
  encodeFloat32(payload, offset, -data.demand_L3); offset += 4;
  
  encodeFloat32(payload, offset, data.import_total); offset += 4;
  encodeFloat32(payload, offset, data.import_L1); offset += 4;
  encodeFloat32(payload, offset, data.import_L2); offset += 4;
  encodeFloat32(payload, offset, data.import_L3); offset += 4;
  
  encodeFloat32(payload, offset, data.export_total); offset += 4;
  encodeFloat32(payload, offset, data.export_L1); offset += 4;
  encodeFloat32(payload, offset, data.export_L2); offset += 4;
  encodeFloat32(payload, offset, data.export_L3); offset += 4;
  
  // Total offset should be exactly 160 bytes
  
  // Calculate and add CRC-16 (MODBUS)
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < 163; i++) {  // CRC over ID + FC + ByteCount + Data
    crc ^= buffer[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }
  buffer[163] = crc & 0xFF;        // CRC Low
  buffer[164] = (crc >> 8) & 0xFF; // CRC High
  
  return true;
}

bool compareMessages(const uint8_t* original, const uint8_t* reencoded, size_t length) {
  bool identical = true;
  int differences = 0;
  
  for (size_t i = 0; i < length; i++) {
    if (original[i] != reencoded[i]) {
      differences++;
      identical = false;
    }
  }
  
  return identical;
}


// Detailed frame diff utilities and comparison
static inline uint16_t crc16_modbus(const uint8_t* p, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else              crc >>= 1;
    }
  }
  return crc;
}

static void dumpFrame(const char* tag, const uint8_t* buf, size_t len) {
  if (!buf || len == 0) return;
  uint16_t crcGiven = len >= 2 ? (uint16_t)buf[len-2] | ((uint16_t)buf[len-1] << 8) : 0;
  uint16_t crcCalc  = len >= 2 ? crc16_modbus(buf, len-2) : 0;
  Serial.printf("%s: id=%u fc=0x%02X len=%u bc=%u crc(given=%04X calc=%04X)%s\n",
                tag, buf[0], buf[1], (unsigned)len, (len>3?buf[2]:0),
                crcGiven, crcCalc, (crcGiven==crcCalc?" OK":" MISMATCH"));
  for (size_t i = 0; i < len; i += 16) {
    Serial.printf("  %03u: ", (unsigned)i);
    for (size_t j = 0; j < 16 && (i+j) < len; ++j) Serial.printf("%02X ", buf[i+j]);
    Serial.println();
  }
}

// Single-line hex dump helper (compact)
static void printFrameHexLine(const char* tag, const uint8_t* buf, size_t len) {
  if (!buf || len == 0) return;
  Serial.printf("%s HEX: ", tag);
  for (size_t i = 0; i < len; ++i) Serial.printf("%02X", buf[i]);
  Serial.println();
}

static const char* fieldNameByIndex(int idx) {
  switch (idx) {
    case  0: return "I_L1";        case  1: return "I_L2";        case  2: return "I_L3";
    case  3: return "U_LN_AVG";    case  4: return "U_L1N";       case  5: return "U_L2N";       case  6: return "U_L3N";
    case  7: return "U_LL_AVG";    case  8: return "U_L1L2";      case  9: return "U_L2L3";      case 10: return "U_L3L1";      case 11: return "FREQ";
    case 12: return "P_TOT(-)";    case 13: return "P_L1(-)";     case 14: return "P_L2(-)";     case 15: return "P_L3(-)";
    case 16: return "Q_TOT";       case 17: return "Q_L1";        case 18: return "Q_L2";        case 19: return "Q_L3";
    case 20: return "S_TOT";       case 21: return "S_L1";        case 22: return "S_L2";        case 23: return "S_L3";
    case 24: return "PF_TOT";      case 25: return "PF_L1";       case 26: return "PF_L2";       case 27: return "PF_L3";
    case 28: return "DMD_TOT(-)";  case 29: return "DMD_L1(-)";   case 30: return "DMD_L2(-)";   case 31: return "DMD_L3(-)";
    case 32: return "E_IMP_T";     case 33: return "E_IMP_L1";    case 34: return "E_IMP_L2";    case 35: return "E_IMP_L3";
    case 36: return "E_EXP_T";     case 37: return "E_EXP_L1";    case 38: return "E_EXP_L2";    case 39: return "E_EXP_L3";
    default: return "?";
  }
}

bool compareMessagesDetailed(const uint8_t* original, const uint8_t* reencoded, size_t length) {
  if (!original || !reencoded || length < 5) return false;

  bool identical = true;
  int differences = 0;
  for (size_t i = 0; i < length; i++) {
    if (original[i] != reencoded[i]) { identical = false; differences++; }
  }
  if (identical) {
    Serial.println("\u2713 Message Comparison: IDENTICAL");
    return true;
  }

  Serial.printf("\u2717 Message Comparison: %d byte differences\n", differences);
  dumpFrame("ORIG", original, length);
  dumpFrame("RENC", reencoded, length);

  int crcDiffs = 0, headerDiffs = 0, payloadDiffs = 0, shown = 0;
  for (size_t i = 0; i < length; i++) {
    if (original[i] == reencoded[i]) continue;
    bool isCRC = (i >= length - 2);
    bool isHeader = (i < 3);
    if (isCRC) crcDiffs++; else if (isHeader) headerDiffs++; else payloadDiffs++;

    if (shown < 24) {
      if (!isHeader && !isCRC) {
        int p = (int)i - 3;   // payload offset
        int fidx = p / 4;     // float index 0..39
        int foff = p % 4;     // byte within float
        const uint8_t* payA = &original[3];
        const uint8_t* payB = &reencoded[3];
        float fa = parseFloat32(payA, fidx * 4);
        float fb = parseFloat32(payB, fidx * 4);
      }
      shown++;
    }
  }

  return false;
}

void printDTSU666Data(const DTSU666Data& data) {
  Serial.printf("📊 DTSU: %.0fW | %.1f/%.1f/%.1fV | %.3f/%.3f/%.3fA\n",
                data.power_total,
                data.voltage_L1N, data.voltage_L2N, data.voltage_L3N,
                data.current_L1, data.current_L2, data.current_L3);

  // DEBUG: DTSU Power Breakdown
  // Serial.printf("🔍 DEBUG DTSU BREAKDOWN: Total=%.1fW (L1=%.1f + L2=%.1f + L3=%.1f = %.1fW)\n",
  //              data.power_total, data.power_L1, data.power_L2, data.power_L3,
  //              data.power_L1 + data.power_L2 + data.power_L3);

  if (mqttDataReceived) {
    float mqtt_net_power = (mqttSensorData.po - mqttSensorData.pi) * 1000.0f;
    Serial.printf("📡 L&G: %.0fW | %.1f/%.1f/%.1fV | %.3f/%.3f/%.3fA\n",
                  mqtt_net_power,
                  mqttSensorData.u1, mqttSensorData.u2, mqttSensorData.u3,
                  mqttSensorData.i1, mqttSensorData.i2, mqttSensorData.i3);

    // DEBUG: Power Comparison at Print Stage
    float power_diff = data.power_total - mqtt_net_power;
    // Serial.printf("🔍 DEBUG COMPARISON: DTSU=%.0fW, MQTT=%.0fW, Diff=%.0fW\n",
    //              data.power_total, mqtt_net_power, power_diff);
  }
}

// Auto-restart and health monitoring functions
void reportSystemError(const char* errorType, const char* errorMessage, int errorCode = 0) {
  // Log to serial
  Serial.printf("🚨 SYSTEM ERROR [%s]: %s", errorType, errorMessage);
  if (errorCode != 0) {
    Serial.printf(" (code: %d)", errorCode);
  }
  Serial.println();

  // Report via MQTT if connected
  if (mqttClient.connected()) {
    DynamicJsonDocument errorDoc(512);
    errorDoc["timestamp"] = millis();
    errorDoc["error_type"] = errorType;
    errorDoc["error_message"] = errorMessage;
    if (errorCode != 0) {
      errorDoc["error_code"] = errorCode;
    }
    errorDoc["free_heap"] = ESP.getFreeHeap();
    errorDoc["uptime"] = millis() / 1000;
    errorDoc["restart_count"] = systemHealth.totalRestarts;

    String errorPayload;
    serializeJson(errorDoc, errorPayload);
    mqttClient.publish("MBUS-PROXY/ERROR", errorPayload.c_str());
  }
}

void updateTaskHealthbeat(bool isProxyTask) {
  uint32_t currentTime = millis();
  if (isProxyTask) {
    systemHealth.proxyTaskLastSeen = currentTime;
  } else {
    systemHealth.mqttTaskLastSeen = currentTime;
  }
}

void performHealthCheck() {
  uint32_t currentTime = millis();
  uint32_t freeHeap = ESP.getFreeHeap();
  bool needsRestart = false;

  // Check task watchdogs
  if (currentTime - systemHealth.proxyTaskLastSeen > WATCHDOG_TIMEOUT_MS) {
    reportSystemError("WATCHDOG", "Proxy task hung - restarting system");
    needsRestart = true;
  }

  if (currentTime - systemHealth.mqttTaskLastSeen > WATCHDOG_TIMEOUT_MS) {
    reportSystemError("WATCHDOG", "MQTT task hung - restarting system");
    needsRestart = true;
  }

  // Check failure thresholds
  if (systemHealth.evccConsecutiveFailures >= EVCC_FAILURE_THRESHOLD) {
    reportSystemError("EVCC_API", "Too many consecutive EVCC API failures - restarting", systemHealth.evccConsecutiveFailures);
    needsRestart = true;
  }

  if (systemHealth.mqttConsecutiveFailures >= MQTT_FAILURE_THRESHOLD) {
    reportSystemError("MQTT", "Too many consecutive MQTT failures - restarting", systemHealth.mqttConsecutiveFailures);
    needsRestart = true;
  }

  if (systemHealth.modbusConsecutiveFailures >= MODBUS_FAILURE_THRESHOLD) {
    reportSystemError("MODBUS", "Too many consecutive MODBUS failures - restarting", systemHealth.modbusConsecutiveFailures);
    needsRestart = true;
  }

  // Check memory
  if (freeHeap < MIN_FREE_HEAP) {
    reportSystemError("MEMORY", "Low heap memory - restarting", freeHeap);
    needsRestart = true;
  }

  // Report health status via MQTT
  if (mqttClient.connected() && (currentTime - systemHealth.lastHealthCheck) > HEALTH_CHECK_INTERVAL_MS) {
    DynamicJsonDocument healthDoc(512);
    healthDoc["timestamp"] = currentTime;
    healthDoc["proxy_task_age"] = currentTime - systemHealth.proxyTaskLastSeen;
    healthDoc["mqtt_task_age"] = currentTime - systemHealth.mqttTaskLastSeen;
    healthDoc["evcc_failures"] = systemHealth.evccConsecutiveFailures;
    healthDoc["mqtt_failures"] = systemHealth.mqttConsecutiveFailures;
    healthDoc["modbus_failures"] = systemHealth.modbusConsecutiveFailures;
    healthDoc["free_heap"] = freeHeap;
    healthDoc["uptime"] = currentTime / 1000;
    healthDoc["restart_count"] = systemHealth.totalRestarts;
    healthDoc["healthy"] = !needsRestart;

    String healthPayload;
    serializeJson(healthDoc, healthPayload);
    mqttClient.publish("MBUS-PROXY/HEALTH", healthPayload.c_str());

    systemHealth.lastHealthCheck = currentTime;
  }

  // Restart system if needed
  if (needsRestart) {
    systemHealth.totalRestarts++;
    Serial.printf("🔄 SYSTEM RESTART #%lu in 5 seconds...\n", systemHealth.totalRestarts);
    delay(5000);  // Give time for MQTT message to send
    ESP.restart();
  }
}

// Function prototypes
void printMeterComparisonTable();
bool compareMessages(const uint8_t* original, const uint8_t* reencoded, size_t length);
bool compareMessagesDetailed(const uint8_t* original, const uint8_t* reencoded, size_t length);
static void printBasicMessage(const ModbusMessage& msg);
static void maybeDecodeByLastRequest(const ModbusMessage& msg);
static void printCompactComparison();
void calculatePowerCorrection();

// ---- MQTT helper utilities ----
static float readAliased(JsonObject obj, const char* const* aliases, size_t n, float defVal) {
  for (size_t i = 0; i < n; ++i) {
    const char* k = aliases[i];
    if (!k) continue;
    if (!obj.containsKey(k)) continue;

    JsonVariant v = obj[k];
    // Numeric types directly
    if (v.is<float>() || v.is<double>() || v.is<long>() || v.is<int>()) {
      return v.as<float>();
    }
    // Strings like "234.5" or "234.5 V" or with comma decimal
    if (v.is<const char*>() || v.is<String>()) {
      String s = v.as<String>();
      // Replace comma with dot for decimal comma locales
      s.replace(',', '.');
      const char* c = s.c_str();
      // Skip leading non-numeric (except sign)
      const char* p = c;
      while (*p && !(*p == '-' || (*p >= '0' && *p <= '9'))) ++p;
      if (*p) {
        char* endptr = nullptr;
        float val = strtof(p, &endptr);
        if (endptr != p) return val;
      }
    }
  }
  return defVal;
}

static void logJsonObjectKeys(const char* prefix, JsonObject obj) {
  Serial.print(prefix);
  bool first = true;
  for (JsonPair kv : obj) {
    if (!first) Serial.print(", ");
    Serial.print(kv.key().c_str());
    first = false;
  }
  Serial.println();
}

// (moved earlier) printCompactComparison definition

// Thread-safe shared DTSU data management functions
bool isSharedDataValid() {
  if (xSemaphoreTake(sharedDTSU.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
    bool valid = sharedDTSU.valid;
    uint32_t age = millis() - sharedDTSU.timestamp;
    xSemaphoreGive(sharedDTSU.mutex);

    return valid && (age < DATA_VALIDITY_MS);
  }
  return false;
}

bool serveSharedResponse(const ModbusMessage& request) {
  if (xSemaphoreTake(sharedDTSU.mutex, 20 / portTICK_PERIOD_MS) == pdTRUE) {
    if (sharedDTSU.valid && (millis() - sharedDTSU.timestamp < DATA_VALIDITY_MS)) {
      // Serve the shared response directly to SUN2000
      uint32_t replyStart = millis();
      ModbusMessage sharedMsg;
      sharedMsg.raw = sharedDTSU.responseBuffer;
      sharedMsg.len = sharedDTSU.responseLength;
      sharedMsg.type = MBType::Reply;

      sharedDTSU.serveCount++;
      xSemaphoreGive(sharedDTSU.mutex);

      if (modbusSUN.write(sharedMsg)) {
        uint32_t replyTime = millis() - replyStart;
        Serial.printf("🚀→SUN (%lums)", replyTime);
        return true;
      }
    } else {
      xSemaphoreGive(sharedDTSU.mutex);
    }
  }
  return false;
}

void updateSharedData(const uint8_t* responseData, uint16_t responseLen, const DTSU666Data& parsedData) {
  if (xSemaphoreTake(sharedDTSU.mutex, 50 / portTICK_PERIOD_MS) == pdTRUE) {
    if (responseLen <= sizeof(sharedDTSU.responseBuffer)) {
      memcpy(sharedDTSU.responseBuffer, responseData, responseLen);
      sharedDTSU.responseLength = responseLen;
      sharedDTSU.parsedData = parsedData;
      sharedDTSU.timestamp = millis();
      sharedDTSU.valid = true;
      sharedDTSU.updateCount++;
    } else {
      Serial.printf("⚠️ Response too large for shared data: %u bytes\n", responseLen);
      sharedDTSU.valid = false;
    }
    xSemaphoreGive(sharedDTSU.mutex);
  }
}

void incrementSharedErrorCount() {
  if (xSemaphoreTake(sharedDTSU.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
    sharedDTSU.errorCount++;
    xSemaphoreGive(sharedDTSU.mutex);
  }
}

void printSharedStats() {
  if (xSemaphoreTake(sharedDTSU.mutex, 50 / portTICK_PERIOD_MS) == pdTRUE) {
    uint32_t updates = sharedDTSU.updateCount;
    uint32_t errors = sharedDTSU.errorCount;
    uint32_t serves = sharedDTSU.serveCount;
    uint32_t age = sharedDTSU.valid ? (millis() - sharedDTSU.timestamp) : 0;
    xSemaphoreGive(sharedDTSU.mutex);

    if (updates > 0) {
      float successRate = ((float)(updates - errors) / updates) * 100.0f;
      Serial.printf("📊 Shared data: %.1f%% success, %lums age\n", successRate, age);
    }
  }
}

// DTSU-666 response parsing function
bool parseDTSU666Response(const uint8_t* raw, uint16_t len, DTSU666Data& data) {
  if (!raw || len < 165) return false;

  // Skip MODBUS header (ID, FC, ByteCount) - data starts at offset 3
  const uint8_t* payload = raw + 3;

  // Parse IEEE 754 float values (4 bytes each, big-endian)
  auto parseFloat = [](const uint8_t* bytes) -> float {
    uint32_t bits = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                    (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
    return *(float*)&bits;
  };

  // Parse based on correct register map starting at 2102
  // Offset calculation: (RegisterAddress - 2102) * 4 bytes per FP32

  // 2102-2106: Currents (A)
  data.current_L1 = parseFloat(payload + 0);        // 2102: Current L1
  data.current_L2 = parseFloat(payload + 4);        // 2104: Current L2
  data.current_L3 = parseFloat(payload + 8);        // 2106: Current L3

  // 2108-2114: Voltages (V)
  data.voltage_LN_avg = parseFloat(payload + 12);   // 2108: Average L-N voltage
  data.voltage_L1N = parseFloat(payload + 16);      // 2110: L1-N voltage
  data.voltage_L2N = parseFloat(payload + 20);      // 2112: L2-N voltage
  data.voltage_L3N = parseFloat(payload + 24);      // 2114: L3-N voltage

  // 2116-2124: Line-to-line voltages and frequency
  data.voltage_LL_avg = parseFloat(payload + 28);   // 2116: Average L-L voltage
  data.voltage_L1L2 = parseFloat(payload + 32);     // 2118: L1-L2 voltage
  data.voltage_L2L3 = parseFloat(payload + 36);     // 2120: L2-L3 voltage
  data.voltage_L3L1 = parseFloat(payload + 40);     // 2122: L3-L1 voltage
  data.frequency = parseFloat(payload + 44);        // 2124: Grid frequency

  // 2126-2132: Active power (W) - INVERTED to simulate production
  data.power_total = parseFloat(payload + 48);      // 2126: Total active power
  data.power_L1 = parseFloat(payload + 52);         // 2128: L1 active power
  data.power_L2 = parseFloat(payload + 56);         // 2130: L2 active power
  data.power_L3 = parseFloat(payload + 60);         // 2132: L3 active power

  // 2134-2140: Reactive power (var)
  data.reactive_total = parseFloat(payload + 64);   // 2134: Total reactive power
  data.reactive_L1 = parseFloat(payload + 68);      // 2136: L1 reactive power
  data.reactive_L2 = parseFloat(payload + 72);      // 2138: L2 reactive power
  data.reactive_L3 = parseFloat(payload + 76);      // 2140: L3 reactive power

  // 2142-2148: Apparent power (VA)
  data.apparent_total = parseFloat(payload + 80);   // 2142: Total apparent power
  data.apparent_L1 = parseFloat(payload + 84);      // 2144: L1 apparent power
  data.apparent_L2 = parseFloat(payload + 88);      // 2146: L2 apparent power
  data.apparent_L3 = parseFloat(payload + 92);      // 2148: L3 apparent power

  return true;
}

// Apply power correction to raw MODBUS response
bool applyPowerCorrection(uint8_t* raw, uint16_t len, float correction) {
  if (!raw || len < 165) return false;

  // Skip MODBUS header - data starts at offset 3
  uint8_t* payload = raw + 3;

  // Apply correction to power values (IEEE 754 float modification)
  auto modifyFloat = [](uint8_t* bytes, float correction) {
    uint32_t bits = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                    (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
    float original = *(float*)&bits;
    float corrected = original + correction;
    uint32_t newBits = *(uint32_t*)&corrected;
    bytes[0] = (newBits >> 24) & 0xFF;
    bytes[1] = (newBits >> 16) & 0xFF;
    bytes[2] = (newBits >> 8) & 0xFF;
    bytes[3] = newBits & 0xFF;
  };

  // Apply correction proportionally based on existing power distribution
  auto parseFloat = [](const uint8_t* bytes) -> float {
    uint32_t bits = (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
                    (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
    return *(float*)&bits;
  };

  float totalPower = parseFloat(payload + 48);
  float l1Power = parseFloat(payload + 52);
  float l2Power = parseFloat(payload + 56);
  float l3Power = parseFloat(payload + 60);

  // Calculate proportional distribution based on existing loads
  float l1Ratio = (totalPower > 0) ? (l1Power / totalPower) : 0.33f;
  float l2Ratio = (totalPower > 0) ? (l2Power / totalPower) : 0.33f;
  float l3Ratio = (totalPower > 0) ? (l3Power / totalPower) : 0.33f;

  // Apply corrections proportionally
  modifyFloat(payload + 48, correction);                    // 2126: Total Active Power (full correction)
  modifyFloat(payload + 52, correction * l1Ratio);         // 2128: L1 Active Power (proportional)
  modifyFloat(payload + 56, correction * l2Ratio);         // 2130: L2 Active Power (proportional)
  modifyFloat(payload + 60, correction * l3Ratio);         // 2132: L3 Active Power (proportional)

  // Recalculate CRC for modified response
  uint16_t newCrc = ModbusRTU485::crc16(raw, len - 2);
  raw[len - 2] = newCrc & 0xFF;
  raw[len - 1] = (newCrc >> 8) & 0xFF;

  return true;
}

// Wallbox power correction calculation function
void calculatePowerCorrection() {
  float wallboxPower = 0.0f;
  uint32_t dataAge = 0;

  // Thread-safe access to EVCC data
  if (!getEVCCChargePower(wallboxPower, dataAge)) {
    // No valid EVCC API data available or data too old
    powerCorrection = 0.0f;
    powerCorrectionActive = false;
    // Serial.println("🔍 DEBUG: No valid EVCC API data - power correction disabled");
    return;
  }

  // DEBUG: EVCC API Data with age info
  // Serial.printf("🔍 DEBUG WALLBOX POWER: %.0fW (from EVCC API, age: %.1fs)\n",
  //              wallboxPower, dataAge / 1000.0f);

  // Get current DTSU-666 total power (if available)
  float dtsuTotalPower = dtsu666Data.power_total;

  // DEBUG: Power Correction Input Values
  // Serial.printf("🔍 DEBUG POWER CORRECTION: Wallbox=%.0fW, DTSU=%.0fW\n", wallboxPower, dtsuTotalPower);

  // Apply correction only if wallbox power exceeds threshold
  if (fabs(wallboxPower) > CORRECTION_THRESHOLD) {
    powerCorrection = wallboxPower; // Positive to add wallbox load (between DTSU and grid)
    powerCorrectionActive = true;
    lastCorrectionTime = millis();

    // Serial.printf("\n⚡ POWER CORRECTION ACTIVATED:\n");
    // Serial.printf("   Wallbox Power: %.0fW | DTSU Power: %.0fW\n", wallboxPower, dtsuTotalPower);
    // Serial.printf("   Correction Applied: +%.0fW (adding wallbox load between DTSU and grid)\n", powerCorrection);
    // Serial.printf("   Wallbox charging detected!\n");
  } else {
    // Deactivate correction if wallbox power is below threshold
    if (powerCorrectionActive) {
      Serial.printf("\n⚡ POWER CORRECTION DEACTIVATED:\n");
      // Serial.printf("   Wallbox Power: %.0fW (≤ %.0fW threshold)\n", wallboxPower, CORRECTION_THRESHOLD);
      Serial.printf("   No significant wallbox charging detected\n");
    }

    powerCorrection = 0.0f;
    powerCorrectionActive = false;
  }

  // Serial.printf("   Power Correction: %s | Wallbox: %.0fW | DTSU: %.0fW | Correction: %.0fW\n",
  //              powerCorrectionActive ? "ACTIVE" : "INACTIVE",
  //              wallboxPower, dtsuTotalPower, powerCorrection);
}


// FreeRTOS Task Prototypes
void mqttTask(void *pvParameters);
void proxyTask(void *pvParameters);
void watchdogTask(void *pvParameters);

// MQTT message callback function (removed - using EVCC API instead)

// WiFi and MQTT setup functions
void setupWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.printf("WiFi connected! IP address: %s\n", WiFi.localIP().toString().c_str());
}

void setupMQTT() {
  Serial.printf("🔗 Setting up MQTT connection to %s:1883\n", mqttServer);
  
  // Configure MQTT client with larger buffer and keep-alive settings (publishing only)
  mqttClient.setServer(mqttServer, 1883);
  // Removed callback - no longer receiving MQTT messages
  mqttClient.setBufferSize(2048);  // Increase buffer size for publishing
  mqttClient.setKeepAlive(15);     // Keep-alive interval in seconds
  
  Serial.println("🔧 MQTT Client Configuration:");
  Serial.printf("   Buffer Size: 2048 bytes\n");
  Serial.printf("   Keep-Alive: 15 seconds\n");
  Serial.printf("   Callback function: %s\n", "onMqttMessage");
  
  int attempts = 0;
  while (!mqttClient.connected()) {
    attempts++;
    Serial.printf("🔌 MQTT connection attempt #%d...", attempts);
    
    if (mqttClient.connect("ESP32_ModbusProxy")) {
      Serial.println(" ✅ CONNECTED!");
      Serial.printf("   Client ID: ESP32_ModbusProxy\n");
      Serial.printf("   MQTT State: %d (0=connected)\n", mqttClient.state());
      
      Serial.println("📡 MQTT configured for publishing only (no subscriptions)");
      Serial.println("   Using EVCC API for wallbox data instead of MQTT messages");
    } else {
      Serial.printf(" ❌ FAILED (rc=%d)\n", mqttClient.state());
      Serial.println("   MQTT Error Codes:");
      Serial.println("   -4: Connection timeout");
      Serial.println("   -3: Connection lost");
      Serial.println("   -2: Connect failed");
      Serial.println("   -1: Disconnected");
      Serial.println("   1-5: Protocol/auth errors");
      Serial.println("   Retrying in 5 seconds...");
      delay(5000);
    }
  }
}

// Function to queue corrected power data for MQTT publishing
void queueCorrectedPowerData(const DTSU666Data& correctedData, const DTSU666Data& originalData, bool correctionApplied = false, float correctionValue = 0.0f) {
  MQTTPublishItem item;
  item.correctedData = correctedData;
  item.originalData = originalData;
  item.correctionApplied = correctionApplied;
  item.correctionValue = correctionValue;
  item.timestamp = millis();

  // Send to queue (non-blocking, discard if queue full)
  if (xQueueSend(mqttPublishQueue, &item, 0) != pdTRUE) {
    Serial.println("⚠️ MQTT publish queue full, dropping data");
  }
}

// MQTT publishing function for corrected power data
void publishCorrectedPowerData(const DTSU666Data& correctedData, const DTSU666Data& originalData, bool correctionApplied = false, float correctionValue = 0.0f) {
  if (!mqttClient.connected()) {
    return; // Don't publish if MQTT is not connected
  }

  // Create JSON payload with corrected power data
  DynamicJsonDocument doc(512);
  doc["timestamp"] = millis();
  doc["device"] = "ESP32_ModbusProxy";
  doc["source"] = "DTSU-666_corrected";


  // SUN2000 values (corrected values sent to inverter)
  doc["SUN2000_power_total"] = correctedData.power_total;
  doc["SUN2000_power_L1"] = correctedData.power_L1;
  doc["SUN2000_power_L2"] = correctedData.power_L2;
  doc["SUN2000_power_L3"] = correctedData.power_L3;

  // DTSU values (original readings from DTSU-666 meter)
  doc["DTSU_power_total"] = originalData.power_total;
  doc["DTSU_power_L1"] = originalData.power_L1;
  doc["DTSU_power_L2"] = originalData.power_L2;
  doc["DTSU_power_L3"] = originalData.power_L3;

  // DEBUG: MQTT Publishing Output Values
  // Serial.printf("🔍 DEBUG MQTT PUB OUTPUT: SUN2000=%.1fW, DTSU=%.1fW, Wallbox=%.1fW\n",
  //              correctedData.power_total, originalData.power_total, correctionValue);

  // Additional useful data
  doc["voltage_L1N"] = correctedData.voltage_L1N;
  doc["voltage_L2N"] = correctedData.voltage_L2N;
  doc["voltage_L3N"] = correctedData.voltage_L3N;
  doc["current_L1"] = correctedData.current_L1;
  doc["current_L2"] = correctedData.current_L2;
  doc["current_L3"] = correctedData.current_L3;
  doc["frequency"] = correctedData.frequency;

  // Wallbox power consumption (difference between meters)
  doc["correction_applied"] = correctionApplied;
  doc["Wallbox_power"] = correctionValue;

  // Serialize to string
  String payload;
  serializeJson(doc, payload);

  // Publish to MBUS-PROXY/DATA topic
  if (mqttClient.publish("MBUS-PROXY/DATA", payload.c_str())) {
  } else {
    Serial.println("❌ MQTT: Failed to publish corrected power data");
  }
}

// Setup function
// Simple direct proxy task - back to basics for debugging
void proxyTask(void *pvParameters) {
  Serial.println("🐌 Simple Proxy Task started - Direct SUN2000 ↔ DTSU proxying");

  ModbusMessage sunMsg;
  uint32_t proxyCount = 0;

  while (true) {
    // Update task heartbeat
    updateTaskHealthbeat(true);

    // Wait for SUN2000 request with timeout
    if (modbusSUN.read(sunMsg, 2000)) {
      proxyCount++;
      uint32_t proxyStart = millis();

      // Only proxy requests for slave ID 11 (DTSU-666)
      if (sunMsg.id == 11 && sunMsg.type == MBType::Request) {
        // Serial.printf("\n🔵 PROXY#%lu: SUN→ ID=%u FC=0x%02X len=%u ",
        //              proxyCount, sunMsg.id, sunMsg.fc, sunMsg.len);

        // // Print raw frame for debugging
        // Serial.print("RAW=[");
        // for (uint16_t i = 0; i < sunMsg.len; i++) {
        //   if (i > 0) Serial.print(" ");
        //   Serial.printf("%02X", sunMsg.raw[i]);
        // }
        // Serial.print("]");
        // Serial.print(" ✅PROXY→DTSU");

        // Forward raw frame to DTSU

        // Use raw write to preserve exact frame
        uint32_t dtsuStart = millis();
        size_t written = SerialDTU.write(sunMsg.raw, sunMsg.len);
        SerialDTU.flush();

        if (written == sunMsg.len) {

          // Wait for DTSU reply
          ModbusMessage dtsuMsg;
          if (modbusDTU.read(dtsuMsg, 1000)) {
            uint32_t dtsuTime = millis() - dtsuStart;

            // Raw response debug (short form)

            if (dtsuMsg.type == MBType::Exception) {
              Serial.printf("   ❌ DTSU EXCEPTION: Code=0x%02X\n", dtsuMsg.exCode);
              systemHealth.modbusConsecutiveFailures++;
              reportSystemError("MODBUS", "DTSU exception", dtsuMsg.exCode);
            } else if (dtsuMsg.fc == 0x03 && dtsuMsg.len >= 165) {
              // This is a main data response - decode and apply corrections
              DTSU666Data dtsuData;
              if (parseDTSU666Response(dtsuMsg.raw, dtsuMsg.len, dtsuData)) {

                // Update global DTSU data and calculate power correction if MQTT data available
                dtsu666Data = dtsuData;

                // Recalculate power correction using EVCC API data
                calculatePowerCorrection();

                // Apply power correction if needed
                DTSU666Data finalData = dtsuData; // Start with original data
                bool correctionAppliedSuccessfully = false;

                if (powerCorrectionActive && fabs(powerCorrection) >= CORRECTION_THRESHOLD) {
                  // Serial.printf("   🔄 Applying correction: +%.1fW\n", powerCorrection);

                  // Create a copy of the response and apply correction
                  uint8_t correctedResponse[165];
                  memcpy(correctedResponse, dtsuMsg.raw, dtsuMsg.len);

                  if (applyPowerCorrection(correctedResponse, dtsuMsg.len, powerCorrection)) {
                    // Serial.printf("   ✨ Applied +%.1fW correction (proportional distribution)\n", powerCorrection);

                    // Store corrected response in a static buffer (since dtsuMsg.raw points to library buffer)
                    static uint8_t staticCorrectedResponse[165];
                    memcpy(staticCorrectedResponse, correctedResponse, dtsuMsg.len);
                    dtsuMsg.raw = staticCorrectedResponse;  // Use corrected data

                    // Parse the corrected data for final data structure
                    if (parseDTSU666Response(staticCorrectedResponse, dtsuMsg.len, finalData)) {
                      correctionAppliedSuccessfully = true;
                    } else {
                      // If parsing fails, apply correction manually to data structure
                      finalData.power_total += powerCorrection;
                      finalData.power_L1 += powerCorrection / 3.0f;
                      finalData.power_L2 += powerCorrection / 3.0f;
                      finalData.power_L3 += powerCorrection / 3.0f;
                      correctionAppliedSuccessfully = true;
                    }
                  }
                }

                // DEBUG: Before queuing data
                // Serial.printf("🔍 DEBUG QUEUE INPUT: P_total=%.1fW, correction=%s, value=%.1fW\n",
                //              finalData.power_total, correctionAppliedSuccessfully ? "true" : "false",
                //              correctionAppliedSuccessfully ? powerCorrection : 0.0f);

                // Queue corrected data for MQTT publishing (include original data)
                queueCorrectedPowerData(finalData, dtsuData, correctionAppliedSuccessfully, correctionAppliedSuccessfully ? powerCorrection : 0.0f);

                // Get current EVCC API data
                float currentWallboxPower = 0.0f;
                uint32_t apiDataAge = 0;
                bool hasValidApiData = getEVCCChargePower(currentWallboxPower, apiDataAge);

                // Calculate final SUN2000 value
                float sun2000Value = dtsuData.power_total;
                if (correctionAppliedSuccessfully && powerCorrection > 0) {
                  sun2000Value = finalData.power_total;
                }

                // Clean 3-line output
                Serial.printf("DTSU: %.1fW\n", dtsuData.power_total);
                Serial.printf("API:  %.1fW (%s)\n", currentWallboxPower, hasValidApiData ? "valid" : "stale");
                Serial.printf("SUN2000: %.1fW (DTSU %.1fW + correction %.1fW)\n",
                              sun2000Value, dtsuData.power_total, correctionAppliedSuccessfully ? powerCorrection : 0.0f);
              }
            }

            // Forward DTSU reply back to SUN2000
            uint32_t sunReplyStart = millis();
            size_t sunWritten = SerialSUN.write(dtsuMsg.raw, dtsuMsg.len);
            SerialSUN.flush();

            if (sunWritten == dtsuMsg.len) {
              uint32_t proxyTime = millis() - proxyStart;
              uint32_t sunReplyTime = millis() - sunReplyStart;

              // Reset MODBUS failure counter on successful transaction
              systemHealth.modbusConsecutiveFailures = 0;

            } else {
              Serial.printf("   ❌ Failed to write to SUN2000: %u/%u bytes\n", sunWritten, dtsuMsg.len);
              systemHealth.modbusConsecutiveFailures++;
              reportSystemError("MODBUS", "SUN2000 write failed", written);
            }
          } else {
            Serial.println("   ❌ No reply from DTSU (timeout)");
            systemHealth.modbusConsecutiveFailures++;
            reportSystemError("MODBUS", "DTSU timeout", 0);
          }
        } else {
          Serial.printf("   ❌ Failed to write to DTSU: %u/%u bytes\n", written, sunMsg.len);
          systemHealth.modbusConsecutiveFailures++;
          reportSystemError("MODBUS", "DTSU write failed", written);
        }
        Serial.println();
      }
      // Note: No logging for non-DTSU messages (ID != 11) to reduce noise
    }

    // Small delay to prevent task starvation
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    Serial.println("🚀 ESP32 MODBUS PROXY starting...");
    Serial.printf("📅 Build: %s %s | FancyTable=%s\n", __DATE__, __TIME__, PRINT_FANCY_TABLE ? "ON" : "OFF");
    Serial.println("🎯 Mode: Full bidirectional proxy between SUN2000 and DTSU-666");

    // Initialize system health monitoring
    uint32_t currentTime = millis();
    systemHealth.proxyTaskLastSeen = currentTime;
    systemHealth.mqttTaskLastSeen = currentTime;
    systemHealth.evccConsecutiveFailures = 0;
    systemHealth.mqttConsecutiveFailures = 0;
    systemHealth.modbusConsecutiveFailures = 0;
    systemHealth.totalRestarts = 0;
    systemHealth.lastHealthCheck = currentTime;
    systemHealth.systemHealthy = true;
    Serial.println("🏥 System health monitoring initialized");

    // Initialize WiFi and MQTT (Serial output removed from MQTT task)
    setupWiFi();
    setupMQTT();

    // Initialize serial ports for MODBUS proxy operation
    Serial.println("🔧 Initializing RS485 interfaces...");

    SerialSUN.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);
    Serial.printf("   ✅ SUN2000 interface: UART2, %d baud, pins %d(RX)/%d(TX)\n",
                  MODBUS_BAUDRATE, RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);

    SerialDTU.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);
    Serial.printf("   ✅ DTSU-666 interface: UART1, %d baud, pins %d(RX)/%d(TX)\n",
                  MODBUS_BAUDRATE, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);

    // Initialize ModbusRTU485 instances for proxy operation
    Serial.println("🔧 Initializing MODBUS protocol handlers...");

    modbusSUN.begin(SerialSUN, MODBUS_BAUDRATE);
    Serial.println("   ✅ SUN2000 MODBUS handler initialized");

    modbusDTU.begin(SerialDTU, MODBUS_BAUDRATE);
    Serial.println("   ✅ DTSU-666 MODBUS handler initialized");

    // Initialize thread-safe shared data structure
    Serial.println("🔧 Initializing shared data synchronization...");
    sharedDTSU.mutex = xSemaphoreCreateMutex();
    if (sharedDTSU.mutex == NULL) {
        Serial.println("❌ Failed to create mutex! System halted.");
        while(1) { delay(1000); }
    }

    // Initialize MQTT RX semaphore
    sharedMQTTRx.semaphore = xSemaphoreCreateBinary();
    if (sharedMQTTRx.semaphore == NULL) {
        Serial.println("❌ Failed to create MQTT RX semaphore! System halted.");
        while(1) { delay(1000); }
    }
    xSemaphoreGive(sharedMQTTRx.semaphore); // Initialize as available

    // Initialize EVCC data mutex
    sharedEVCC.mutex = xSemaphoreCreateMutex();
    if (sharedEVCC.mutex == NULL) {
        Serial.println("❌ Failed to create EVCC mutex! System halted.");
        while(1) { delay(1000); }
    }

    // Initialize MQTT publish queue (buffer 10 items)
    mqttPublishQueue = xQueueCreate(10, sizeof(MQTTPublishItem));
    if (mqttPublishQueue == NULL) {
        Serial.println("❌ Failed to create MQTT publish queue! System halted.");
        while(1) { delay(1000); }
    }

    Serial.println("   ✅ MQTT semaphore and queue created for thread-safe data sharing");

    // Bootstrap initial DTSU data before starting tasks
    Serial.println("🚀 Bootstrapping initial DTSU data...");
    ModbusMessage dtsuRequest, dtsuReply;
    dtsuRequest.valid = true;          // CRITICAL: Mark as valid for write()
    dtsuRequest.type = MBType::Request;
    dtsuRequest.id = 11;
    dtsuRequest.fc = 0x03;
    dtsuRequest.startAddr = 2001;      // Same as SUN2000 request
    dtsuRequest.qty = 1;               // Single register like SUN2000

    bool bootstrapSuccess = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.printf("   Attempt %d/3: Polling DTSU-666...", attempt);
        Serial.printf("\n      Request: ID=%d FC=0x%02X @0x%04X+%d valid=%s",
                     dtsuRequest.id, dtsuRequest.fc, dtsuRequest.startAddr, dtsuRequest.qty,
                     dtsuRequest.valid ? "true" : "false");

        // Use address 2001 (meter status) with correct CRC
        uint8_t bootstrapFrame[8];
        bootstrapFrame[0] = 0x0B;  // Slave ID
        bootstrapFrame[1] = 0x03;  // Function code
        bootstrapFrame[2] = 0x07;  // Start address high byte (2001 = 0x07D1)
        bootstrapFrame[3] = 0xD1;  // Start address low byte
        bootstrapFrame[4] = 0x00;  // Quantity high byte
        bootstrapFrame[5] = 0x01;  // Quantity low byte (1 register)

        // Calculate MODBUS CRC
        uint16_t crc = 0xFFFF;
        for (int i = 0; i < 6; i++) {
            crc ^= bootstrapFrame[i];
            for (int j = 0; j < 8; j++) {
                if (crc & 0x0001) {
                    crc = (crc >> 1) ^ 0xA001;
                } else {
                    crc >>= 1;
                }
            }
        }
        bootstrapFrame[6] = crc & 0xFF;        // CRC low byte
        bootstrapFrame[7] = (crc >> 8) & 0xFF; // CRC high byte
        if (modbusDTU.write(bootstrapFrame, sizeof(bootstrapFrame))) {
            if (modbusDTU.read(dtsuReply, 500)) { // 500ms timeout for bootstrap
                Serial.printf(" ✅ Status OK");

                if (dtsuReply.type == MBType::Reply) {
                    Serial.printf(" - DTSU responding to status checks!\n");
                    bootstrapSuccess = true;
                    break;
                }
            }
        }
        Serial.println(" ❌ Failed");
        if (attempt < 3) {
            Serial.println("   Retrying in 1 second...");
            delay(1000);
        }
    }

    if (!bootstrapSuccess) {
        Serial.println("⚠️  Bootstrap failed - system will start without initial data");
        Serial.println("   SUN2000 requests will return errors until first DTSU poll succeeds");
    } else {
        Serial.println("   ✅ System ready with initial DTSU data");
    }

    // Create simple direct proxy task (temporarily stopping dual-task architecture)
    Serial.println("🚀 Creating simple direct proxy task for debugging...");

    // Create MQTT task (Core 0, lowest priority)
    xTaskCreatePinnedToCore(
        mqttTask,
        "MQTTTask",
        3072,      // Smaller stack for MQTT
        NULL,
        1,         // Lowest priority
        NULL,
        0          // Core 0
    );
    Serial.println("   ✅ MQTT task created (Core 0, Priority 1)");

    // Create simple direct proxy task (Core 1, medium priority)
    xTaskCreatePinnedToCore(
        proxyTask,
        "SimpleProxy",
        4096,      // Larger stack for all operations
        NULL,
        2,         // Medium priority
        NULL,
        1          // Core 1
    );
    Serial.println("   ✅ Simple Proxy task created (Core 1, Priority 2)");

    // Create independent watchdog task (Core 0, highest priority)
    xTaskCreatePinnedToCore(
        watchdogTask,
        "WatchdogTask",
        2048,      // Small stack for health monitoring
        NULL,
        3,         // Highest priority
        NULL,
        0          // Core 0 (separate from proxy on Core 1)
    );
    Serial.println("   ✅ Watchdog task created (Core 0, Priority 3)");

    Serial.println("🔗 Simple direct proxy initialized for debugging!");
    Serial.println("   🐌 Direct SUN2000 ↔ DTSU proxying with full logging");
    Serial.println("   🐕 Independent watchdog monitoring all tasks");
    Serial.println("⚡ Ready for direct MODBUS proxy operations with debugging!");
}

void loop() {
    vTaskDelay(1000);
}

// DTSU message handler with encode/decode validation
void printSniffedMessage(const ModbusMessage& msg, uint32_t timestamp) {
    // Only process DTSU-666 data replies
    if (msg.type == MBType::Reply && msg.byteCount == 160) {
        Serial.println("🔄 DTSU-666 Sniffed Message:");
        Serial.printf("⏱️  %lu ms | Frame: %d bytes\n", timestamp, msg.len);
        
        // Parse the message (decode)
        if (parseDTSU666Reply(msg, dtsu666Data)) {
            printDTSU666Data(dtsu666Data);

            // Re-encode the parsed data to verify integrity
            if (encodeDTSU666Reply(dtsu666Data, reencodedBuffer, sizeof(reencodedBuffer))) {
                Serial.println("✅ Re-encoding successful.");
                compareMessagesDetailed(msg.raw, reencodedBuffer, msg.len);
            } else {
                Serial.println("❌ RE-ENCODING FAILED!");
            }

            // If MQTT data is available, print comparisons
            if (mqttDataReceived) {
                if (PRINT_FANCY_TABLE) {
                    printMeterComparisonTable();
                }
                printCompactComparison();
            }
        } else {
            Serial.println("❌ PARSING FAILED!");
        }
    }
}

static void printBasicMessage(const ModbusMessage& msg) {
    Serial.printf("MB id=%u fc=0x%02X type=%s len=%u",
                  msg.id, msg.fc,
                  msg.type == MBType::Request ? "req" : msg.type == MBType::Reply ? "rep" : msg.type == MBType::Exception ? "exc" : "unk",
                  msg.len);

    // Read Holding/Input Registers
    if (msg.fc == 0x03 || msg.fc == 0x04) {
        if (msg.type == MBType::Request) {
            Serial.printf(" start= %u qty= %u", msg.startAddr, msg.qty);
        } else if (msg.type == MBType::Reply) {
            Serial.printf(" byteCount= %u", msg.byteCount);
        }
    }

    // Write Single Register
    else if (msg.fc == 0x06) {
        if (msg.type == MBType::Request || msg.type == MBType::Reply) {
            Serial.printf(" addr= %u value= 0x%04X (%u)", msg.wrAddr, msg.wrValue, msg.wrValue);
        }
    }

    // Write Multiple Registers
    else if (msg.fc == 0x10) {
        if (msg.type == MBType::Request) {
            Serial.printf(" start= %u qty= %u byteCount= %u", msg.wrAddr, msg.wrQty, msg.wrByteCount);
        } else if (msg.type == MBType::Reply) {
            Serial.printf(" start= %u qty= %u", msg.wrAddr, msg.wrQty);
        }
    }

    Serial.println();
}

static void maybeDecodeByLastRequest(const ModbusMessage& msg) {
    if (!g_lastReq.valid) return;
    if (g_lastReq.id != msg.id) return;
    if (g_lastReq.fc != msg.fc) return;

    // Decode FP32 block if it matches 2102..2181 (80 registers)
    if (g_lastReq.startAddr == DTSU_CURRENT_L1_REG && g_lastReq.qty == 80 && msg.byteCount == 160) {
        if (parseDTSU666Reply(msg, dtsu666Data)) {
            // Only print detailed info when new MQTT package has arrived
            if (!newMqttPackageArrived) {
                return;
            }

            // Print DTSU values
            printDTSU666Data(dtsu666Data);

            // Reset the flag after printing
            newMqttPackageArrived = false;

            // Re-encode and log both raw and prepared frames in hex
            if (encodeDTSU666Reply(dtsu666Data, reencodedBuffer, sizeof(reencodedBuffer))) {
                printFrameHexLine("RAW ", msg.raw, msg.len);
                printFrameHexLine("RENC", reencodedBuffer, msg.len);
                // Quick equality check
                compareMessages(msg.raw, reencodedBuffer, msg.len);
                // Detailed table and compact ASCII comparison
                if (PRINT_FANCY_TABLE) {
                    printMeterComparisonTable();
                }
                printCompactComparison();
            } else {
                Serial.println("Re-encode failed: cannot produce comparison hex.");
            }
        }
        return;
    }

    // Decode U_WORD status or meta if requested
    if ((g_lastReq.startAddr == DTSU_STATUS_REG && g_lastReq.qty == 1 && msg.byteCount == 2) ||
        (g_lastReq.startAddr == DTSU_VERSION_REG && g_lastReq.qty == 10 && msg.byteCount == 20)) {
        DTSU666Meta meta;
        if (parseDTSU666MetaWords(g_lastReq.startAddr, msg, meta)) {
            if (g_lastReq.startAddr == DTSU_STATUS_REG) {
                Serial.printf("Status(2001) = %u%s\n", meta.status, meta.status == 15121 ? " (active energy meter)" : "");
            } else {
                Serial.printf("Meta 2214..2223: ver=%u mode=%u irat=%u urat=%u addr=%u baud=%u type=%u\n",
                              meta.version, meta.connection_mode, meta.irat, meta.urat, meta.address, meta.baud, meta.meter_type);
            }
        }
        return;
    }
}

// Validate message parameters to prevent oversized requests
bool validateMessage(const ModbusMessage& msg) {
    switch (msg.fc) {
        case 0x03:
        case 0x04:
            if (msg.qty > 125) {
                Serial.printf("Error: Number of Registers (%d) too large for read request. Max: 125\n", msg.qty);
                return false;
            }
            break;
        case 0x10:
            if (msg.wrByteCount > 246) {
                Serial.printf("Error: Byte Count (%d) too large for write request. Max: 246\n", msg.wrByteCount);
                return false;
            }
            break;
    }
    return true;
}

// Comparison table printing function
void printMeterComparisonTable() {
    if (!PRINT_FANCY_TABLE) {
        return;
    }
    Serial.println("╔════════════════════════════════════════════════════════════════════════════════════════╗");
    Serial.println("║                            SMART METER COMPARISON TABLE                               ║");
    Serial.println("╠══════════════════════════════╦═══════════════════════╦═══════════════════════╦═══════╣");
    Serial.println("║         PARAMETER            ║      DTSU-666         ║   Landis & Gyr (MQTT) ║  DIFF ║");
    Serial.println("╠══════════════════════════════╬═══════════════════════╬═══════════════════════╬═══════╣");
    
    // Current comparison (A)
    float diff_i1 = dtsu666Data.current_L1 - mqttSensorData.i1;
    float diff_i2 = dtsu666Data.current_L2 - mqttSensorData.i2;
    float diff_i3 = dtsu666Data.current_L3 - mqttSensorData.i3;
    Serial.printf("║ Current L1 (A)               ║ %10.3f        ║ %10.3f        ║%6.3f ║\n", dtsu666Data.current_L1, mqttSensorData.i1, diff_i1);
    Serial.printf("║ Current L2 (A)               ║ %10.3f        ║ %10.3f        ║%6.3f ║\n", dtsu666Data.current_L2, mqttSensorData.i2, diff_i2);
    Serial.printf("║ Current L3 (A)               ║ %10.3f        ║ %10.3f        ║%6.3f ║\n", dtsu666Data.current_L3, mqttSensorData.i3, diff_i3);
    Serial.println("╠══════════════════════════════╬═══════════════════════╬═══════════════════════╬═══════╣");
    
    // Voltage comparison (V)
    float diff_u1 = dtsu666Data.voltage_L1N - mqttSensorData.u1;
    float diff_u2 = dtsu666Data.voltage_L2N - mqttSensorData.u2;
    float diff_u3 = dtsu666Data.voltage_L3N - mqttSensorData.u3;
    Serial.printf("║ Voltage L1N (V)              ║ %10.1f        ║ %10.1f        ║%6.1f ║\n", dtsu666Data.voltage_L1N, mqttSensorData.u1, diff_u1);
    Serial.printf("║ Voltage L2N (V)              ║ %10.1f        ║ %10.1f        ║%6.1f ║\n", dtsu666Data.voltage_L2N, mqttSensorData.u2, diff_u2);
    Serial.printf("║ Voltage L3N (V)              ║ %10.1f        ║ %10.1f        ║%6.1f ║\n", dtsu666Data.voltage_L3N, mqttSensorData.u3, diff_u3);
    Serial.println("╠══════════════════════════════╬═══════════════════════╬═══════════════════════╬═══════╣");
    
    // Total Power comparison (W) - Calculate DTSU total from phases, L&G net as Po-Pi
    float dtsu_total_power = dtsu666Data.power_L1 + dtsu666Data.power_L2 + dtsu666Data.power_L3;
    float lg_net_power = (mqttSensorData.po - mqttSensorData.pi) * 1000.0f; // Po-Pi: Export-Import, convert kW to W
    float power_diff = dtsu_total_power - lg_net_power;
    Serial.printf("║ Total Power (W)              ║ %10.0f        ║ %10.0f        ║%6.0f ║\n", dtsu_total_power, lg_net_power, power_diff);
    Serial.println("╠══════════════════════════════╬═══════════════════════╬═══════════════════════╬═══════╣");
    
    // Energy values removed - meters differ as expected
    
    // Additional info
    Serial.printf("║ Frequency (Hz)               ║ %10.2f        ║          N/A          ║  N/A  ║\n", dtsu666Data.frequency);
    Serial.printf("║ Smart Meter ID               ║          N/A          ║ %21s ║  N/A  ║\n", mqttSensorData.smid.c_str());
    Serial.printf("║ Last Update Time             ║          N/A          ║ %21s ║  N/A  ║\n", mqttSensorData.time.c_str());
    Serial.println("╚══════════════════════════════╩═══════════════════════╩═══════════════════════╩═══════╝");
    Serial.println();
}

// Compact ASCII comparison without box-drawing characters
static void printCompactComparison() {
  // DTSU values
  float i1 = dtsu666Data.current_L1;
  float i2 = dtsu666Data.current_L2;
  float i3 = dtsu666Data.current_L3;
  float u1 = dtsu666Data.voltage_L1N;
  float u2 = dtsu666Data.voltage_L2N;
  float u3 = dtsu666Data.voltage_L3N;
  float p_dtsu = dtsu666Data.power_total; // W (already signed per SUN2000 expectation)

  // MQTT values
  float mi1 = mqttSensorData.i1;
  float mi2 = mqttSensorData.i2;
  float mi3 = mqttSensorData.i3;
  float mu1 = mqttSensorData.u1;
  float mu2 = mqttSensorData.u2;
  float mu3 = mqttSensorData.u3;
  float p_mqtt = (mqttSensorData.po - mqttSensorData.pi) * 1000.0f; // W, net export positive per our earlier convention

  // Diffs
  float di1 = i1 - mi1, di2 = i2 - mi2, di3 = i3 - mi3;
  float du1 = u1 - mu1, du2 = u2 - mu2, du3 = u3 - mu3;
  float dp  = p_dtsu - p_mqtt;

  Serial.println("CMP MQTT vs DTSU (ASCII)");
  Serial.printf("  I (A): DTSU %6.3f/%6.3f/%6.3f  MQTT %6.3f/%6.3f/%6.3f  diff %+6.3f/%+6.3f/%+6.3f\n",
                i1,i2,i3, mi1,mi2,mi3, di1,di2,di3);
  Serial.printf("  U (V): DTSU %6.1f/%6.1f/%6.1f  MQTT %6.1f/%6.1f/%6.1f  diff %+6.1f/%+6.1f/%+6.1f\n",
                u1,u2,u3, mu1,mu2,mu3, du1,du2,du3);
  Serial.printf("  P (W): DTSU %7.0f         MQTT %7.0f         diff %+7.0f\n",
                p_dtsu, p_mqtt, dp);

  // Optional per-phase power if MQTT provides import/export per phase
  float p1 = dtsu666Data.power_L1;
  float p2 = dtsu666Data.power_L2;
  float p3 = dtsu666Data.power_L3;
  float mp1 = (mqttSensorData.po1 - mqttSensorData.pi1) * 1000.0f;
  float mp2 = (mqttSensorData.po2 - mqttSensorData.pi2) * 1000.0f;
  float mp3 = (mqttSensorData.po3 - mqttSensorData.pi3) * 1000.0f;
  bool havePhaseP = (mqttSensorData.pi1 != 0.0f || mqttSensorData.po1 != 0.0f ||
                     mqttSensorData.pi2 != 0.0f || mqttSensorData.po2 != 0.0f ||
                     mqttSensorData.pi3 != 0.0f || mqttSensorData.po3 != 0.0f);
  if (havePhaseP) {
    Serial.printf("  Pph (W): DTSU %6.0f/%6.0f/%6.0f  MQTT %6.0f/%6.0f/%6.0f  diff %+6.0f/%+6.0f/%+6.0f\n",
                  p1,p2,p3, mp1,mp2,mp3, p1-mp1, p2-mp2, p3-mp3);
  }
}

// Main sniffer task - continuously listens and prints essential MODBUS traffic

// EVCC HTTP API polling function
bool pollEvccApi() {
    HTTPClient http;
    http.begin(evccApiUrl);
    http.setTimeout(5000);  // 5 second timeout
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); // Follow redirects

    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String payload = http.getString();
        http.end();

        // Parse JSON response
        DynamicJsonDocument doc(8192);  // Increase size for larger JSON
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            Serial.printf("❌ EVCC API: JSON parse error: %s\n", error.c_str());
            systemHealth.evccConsecutiveFailures++;
            reportSystemError("EVCC_API", "JSON parse error", 0);
            updateEVCCChargePower(0.0f, false);
            return false;
        }

        // Extract loadpoints[0].chargePower
        if (doc["loadpoints"].is<JsonArray>() && doc["loadpoints"].size() > 0) {
            JsonObject loadpoint0 = doc["loadpoints"][0];
            if (loadpoint0.containsKey("chargePower")) {
                float chargePower = loadpoint0["chargePower"].as<float>();

                // Thread-safe update of shared EVCC data
                updateEVCCChargePower(chargePower, true);

                // Reset failure counter on success
                systemHealth.evccConsecutiveFailures = 0;

                // Serial.printf("🔌 EVCC API: chargePower=%.0fW\n", chargePower);
                return true;
            } else {
                Serial.println("❌ EVCC API: loadpoints[0] missing chargePower field");
                systemHealth.evccConsecutiveFailures++;
                reportSystemError("EVCC_API", "Missing chargePower field", 0);
                updateEVCCChargePower(0.0f, false);
                return false;
            }
        } else {
            Serial.println("❌ EVCC API: loadpoints array missing or empty");
            systemHealth.evccConsecutiveFailures++;
            reportSystemError("EVCC_API", "Missing loadpoints array", 0);
            updateEVCCChargePower(0.0f, false);
            return false;
        }
    } else {
        Serial.printf("❌ EVCC API: HTTP error %d\n", httpResponseCode);
        systemHealth.evccConsecutiveFailures++;
        reportSystemError("EVCC_API", "HTTP error", httpResponseCode);
        http.end();
        updateEVCCChargePower(0.0f, false);
        return false;
    }
}

// Dedicated MQTT task for handling connections and data processing
void mqttTask(void *pvParameters) {
    (void)pvParameters;
    uint32_t lastReportTime = 0;
    uint32_t mqttReconnectCount = 0;

    Serial.println();
    Serial.println("📡 MQTT TASK STARTED");
    Serial.printf("🧠 Task Info: Core=%d, Priority=%d, Stack=%d bytes\n",
                  xPortGetCoreID(), uxTaskPriorityGet(NULL), uxTaskGetStackHighWaterMark(NULL));
    Serial.println("🔍 Ready to handle MQTT communications...");
    Serial.println();

    while (1) {
        // Update task heartbeat
        updateTaskHealthbeat(false);

        // Handle MQTT connection management
        if (!mqttClient.connected()) {
            mqttReconnectCount++;
            systemHealth.mqttConsecutiveFailures++;
            Serial.printf("🔌 MQTT reconnection attempt #%lu...", mqttReconnectCount);

            if (mqttClient.connect("ESP32_ModbusProxy")) {
                Serial.println(" ✅ CONNECTED!");
                Serial.println("📡 MQTT ready for publishing (no subscriptions)");
                systemHealth.mqttConsecutiveFailures = 0;  // Reset on success
            } else {
                Serial.printf(" ❌ FAILED (state=%d)\n", mqttClient.state());
                reportSystemError("MQTT", "Connection failed", mqttClient.state());
            }
        }

        // Process MQTT messages (non-blocking)
        mqttClient.loop();

        // Check for items in the MQTT publish queue
        MQTTPublishItem item;
        if (xQueueReceive(mqttPublishQueue, &item, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            if (mqttClient.connected()) {
                // Publish the corrected power data
                publishCorrectedPowerData(item.correctedData, item.originalData, item.correctionApplied, item.correctionValue);
            } else {
                Serial.println("⚠️ MQTT not connected, dropping queued data");
            }
        }

        // EVCC HTTP API polling every 10 seconds (independent of MQTT)
        if (millis() - lastHttpPoll > HTTP_POLL_INTERVAL) {
            lastHttpPoll = millis();

            // Serial.println("🌐 Polling EVCC API for wallbox data...");
            bool success = pollEvccApi();

            if (success) {
                // Data is now safely stored in shared structure
                float currentPower = 0.0f;
                uint32_t age = 0;
                if (getEVCCChargePower(currentPower, age)) {
                    // Serial.printf("✅ EVCC API: Retrieved chargePower=%.0fW (stored thread-safe)\n", currentPower);
                }
            } else {
                Serial.println("❌ EVCC API polling failed");
            }
        }

        // Periodic MQTT status report
        if (millis() - lastReportTime > 6000) { // Every 60 seconds
            lastReportTime = millis();
            Serial.printf("\n📊 MQTT STATUS:\n");
            // Serial.printf("   Connection: %s\n", mqttClient.connected() ? "CONNECTED" : "DISCONNECTED");
            // Serial.printf("   Reconnect attempts: %lu\n", mqttReconnectCount);
            // Get current EVCC data for status report
            float currentChargePower = 0.0f;
            uint32_t currentDataAge = 0;
            bool hasValidData = getEVCCChargePower(currentChargePower, currentDataAge);
            // Serial.printf("   EVCC API: %s (%.0fW, age: %.1fs)\n",
            //              hasValidData ? "VALID" : "INVALID",
            //              currentChargePower, currentDataAge / 1000.0f);
            // Serial.printf("   Power correction: %s (%.0fW)\n",
            //              powerCorrectionActive ? "ACTIVE" : "INACTIVE", powerCorrection);
            // Serial.printf("   Free stack: %d bytes\n", uxTaskGetStackHighWaterMark(NULL));
            Serial.println();
        }

        // MQTT task runs at lower frequency than proxy task
        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay for MQTT processing
    }
}

// Independent watchdog task - monitors all other tasks
void watchdogTask(void *pvParameters) {
    (void)pvParameters;
    Serial.println("🐕 Watchdog Task started - Independent system health monitoring");
    Serial.printf("🐕 Running on Core %d with priority %d\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));

    while (true) {
        // Perform independent health check
        performHealthCheck();

        // Run watchdog every 5 seconds (more frequent than the 60s timeout)
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
