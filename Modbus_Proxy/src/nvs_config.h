#pragma once

#include <Arduino.h>

// Log levels
enum LogLevel {
  LOG_LEVEL_DEBUG = 0,
  LOG_LEVEL_INFO = 1,
  LOG_LEVEL_WARN = 2,
  LOG_LEVEL_ERROR = 3
};

// MQTT Configuration structure
struct MQTTConfig {
  char host[64];
  uint16_t port;
  char user[32];
  char pass[32];
  char wallboxTopic[64];
  uint8_t logLevel;
};

// Default configuration values
#define DEFAULT_MQTT_HOST "192.168.0.203"
#define DEFAULT_MQTT_PORT 1883
#define DEFAULT_MQTT_USER "admin"
#define DEFAULT_MQTT_PASS "admin"
#define DEFAULT_WALLBOX_TOPIC "wallbox"
#define DEFAULT_LOG_LEVEL LOG_LEVEL_WARN

// NVS Namespace
#define NVS_NAMESPACE "mbus_config"

// NVS Keys - MQTT
#define NVS_KEY_MQTT_HOST "mqtt_host"
#define NVS_KEY_MQTT_PORT "mqtt_port"
#define NVS_KEY_MQTT_USER "mqtt_user"
#define NVS_KEY_MQTT_PASS "mqtt_pass"
#define NVS_KEY_WB_TOPIC "wb_topic"
#define NVS_KEY_LOG_LEVEL "log_level"

// NVS Keys - WiFi
#define NVS_KEY_WIFI_SSID   "wifi_ssid"
#define NVS_KEY_WIFI_PASS   "wifi_pass"
#define NVS_KEY_DEBUG_MODE  "debug_mode"

// Function declarations - MQTT config
bool initNVSConfig();
bool loadConfig(MQTTConfig& config);
bool saveMQTTCredentials(const char* host, uint16_t port, const char* user, const char* pass);
bool saveWallboxTopic(const char* topic);
bool saveLogLevel(uint8_t level);
bool resetToDefaults();
void getDefaultConfig(MQTTConfig& config);

// Function declarations - WiFi credentials
bool saveWiFiCredentials(const char* ssid, const char* pass);
bool loadWiFiCredentials(char* ssid, size_t ssidLen, char* pass, size_t passLen);
bool hasStoredWiFiCredentials();

// Function declarations - Debug mode
bool isDebugModeEnabled();
void setDebugMode(bool enabled);

// Global config instance
extern MQTTConfig mqttConfig;
extern bool configLoaded;
