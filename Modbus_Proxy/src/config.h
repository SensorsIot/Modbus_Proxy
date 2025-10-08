#pragma once

// Debug settings - Telnet wireless debugging for ESP32-C3
#define ENABLE_SERIAL_DEBUG false
#define ENABLE_TELNET_DEBUG false

// ESP32-C3 Pin definitions - safe GPIOs for UART
#define RS485_SUN2000_RX_PIN 7
#define RS485_SUN2000_TX_PIN 10
#define RS485_DTU_RX_PIN 1
#define RS485_DTU_TX_PIN 0

// Status LED pin (GPIO8, inverted logic - LOW=ON, HIGH=OFF)
#define STATUS_LED_PIN 8
#define LED_INVERTED true

// LED control macros (handles inverted logic)
#if LED_INVERTED
  #define LED_ON()  digitalWrite(STATUS_LED_PIN, LOW)
  #define LED_OFF() digitalWrite(STATUS_LED_PIN, HIGH)
#else
  #define LED_ON()  digitalWrite(STATUS_LED_PIN, HIGH)
  #define LED_OFF() digitalWrite(STATUS_LED_PIN, LOW)
#endif

// MODBUS communication settings
#define MODBUS_BAUDRATE 9600

// DTSU-666 Register definitions
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

// Power correction settings
const float CORRECTION_THRESHOLD = 1000.0f;

// EVCC API settings
const uint32_t HTTP_POLL_INTERVAL = 10000;
const uint32_t EVCC_DATA_MAX_AGE_MS = 10000;

// Task timing constants
const uint32_t WATCHDOG_TIMEOUT_MS = 60000;
const uint32_t HEALTH_CHECK_INTERVAL = 5000;
const uint32_t MQTT_PUBLISH_INTERVAL = 1000;

// Memory thresholds
const uint32_t MIN_FREE_HEAP = 20000;

// MQTT settings
const int mqttPort = 1883;

// MQTT topics - hierarchical structure under MBUS-PROXY
#define MQTT_TOPIC_ROOT "MBUS-PROXY"
#define MQTT_TOPIC_POWER MQTT_TOPIC_ROOT "/power"
#define MQTT_TOPIC_HEALTH MQTT_TOPIC_ROOT "/health"
#define MQTT_TOPIC_STATUS MQTT_TOPIC_ROOT "/status"
#define MQTT_TOPIC_DTSU MQTT_TOPIC_ROOT "/dtsu"
#define MQTT_TOPIC_DEBUG MQTT_TOPIC_ROOT "/debug"