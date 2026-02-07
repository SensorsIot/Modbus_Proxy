#include "nvs_config.h"
#include <Preferences.h>

// Global config instance
MQTTConfig mqttConfig;
bool configLoaded = false;

// Preferences instance for NVS access
static Preferences preferences;

void getDefaultConfig(MQTTConfig& config) {
  strncpy(config.host, DEFAULT_MQTT_HOST, sizeof(config.host) - 1);
  config.host[sizeof(config.host) - 1] = '\0';
  config.port = DEFAULT_MQTT_PORT;
  strncpy(config.user, DEFAULT_MQTT_USER, sizeof(config.user) - 1);
  config.user[sizeof(config.user) - 1] = '\0';
  strncpy(config.pass, DEFAULT_MQTT_PASS, sizeof(config.pass) - 1);
  config.pass[sizeof(config.pass) - 1] = '\0';
  strncpy(config.wallboxTopic, DEFAULT_WALLBOX_TOPIC, sizeof(config.wallboxTopic) - 1);
  config.wallboxTopic[sizeof(config.wallboxTopic) - 1] = '\0';
  config.logLevel = DEFAULT_LOG_LEVEL;
}

bool initNVSConfig() {
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS namespace");
    getDefaultConfig(mqttConfig);
    configLoaded = true;
    return false;
  }

  bool result = loadConfig(mqttConfig);
  preferences.end();
  configLoaded = true;
  return result;
}

bool loadConfig(MQTTConfig& config) {
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    getDefaultConfig(config);
    return false;
  }

  // Load host with default fallback
  String host = preferences.getString(NVS_KEY_MQTT_HOST, DEFAULT_MQTT_HOST);
  strncpy(config.host, host.c_str(), sizeof(config.host) - 1);
  config.host[sizeof(config.host) - 1] = '\0';

  // Load port
  config.port = preferences.getUShort(NVS_KEY_MQTT_PORT, DEFAULT_MQTT_PORT);

  // Load user
  String user = preferences.getString(NVS_KEY_MQTT_USER, DEFAULT_MQTT_USER);
  strncpy(config.user, user.c_str(), sizeof(config.user) - 1);
  config.user[sizeof(config.user) - 1] = '\0';

  // Load password
  String pass = preferences.getString(NVS_KEY_MQTT_PASS, DEFAULT_MQTT_PASS);
  strncpy(config.pass, pass.c_str(), sizeof(config.pass) - 1);
  config.pass[sizeof(config.pass) - 1] = '\0';

  // Load wallbox topic
  String wbTopic = preferences.getString(NVS_KEY_WB_TOPIC, DEFAULT_WALLBOX_TOPIC);
  strncpy(config.wallboxTopic, wbTopic.c_str(), sizeof(config.wallboxTopic) - 1);
  config.wallboxTopic[sizeof(config.wallboxTopic) - 1] = '\0';

  // Load log level
  config.logLevel = preferences.getUChar(NVS_KEY_LOG_LEVEL, DEFAULT_LOG_LEVEL);

  preferences.end();

  Serial.printf("NVS Config loaded: MQTT=%s:%d, User=%s, WB Topic=%s, LogLevel=%d\n",
                config.host, config.port, config.user, config.wallboxTopic, config.logLevel);

  return true;
}

bool saveMQTTCredentials(const char* host, uint16_t port, const char* user, const char* pass) {
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for writing MQTT credentials");
    return false;
  }

  bool success = true;

  if (host && strlen(host) > 0) {
    success &= preferences.putString(NVS_KEY_MQTT_HOST, host);
    strncpy(mqttConfig.host, host, sizeof(mqttConfig.host) - 1);
    mqttConfig.host[sizeof(mqttConfig.host) - 1] = '\0';
  }

  if (port > 0) {
    success &= preferences.putUShort(NVS_KEY_MQTT_PORT, port);
    mqttConfig.port = port;
  }

  if (user && strlen(user) > 0) {
    success &= preferences.putString(NVS_KEY_MQTT_USER, user);
    strncpy(mqttConfig.user, user, sizeof(mqttConfig.user) - 1);
    mqttConfig.user[sizeof(mqttConfig.user) - 1] = '\0';
  }

  if (pass && strlen(pass) > 0) {
    success &= preferences.putString(NVS_KEY_MQTT_PASS, pass);
    strncpy(mqttConfig.pass, pass, sizeof(mqttConfig.pass) - 1);
    mqttConfig.pass[sizeof(mqttConfig.pass) - 1] = '\0';
  }

  preferences.end();

  if (success) {
    Serial.printf("MQTT credentials saved: %s:%d, user=%s\n", mqttConfig.host, mqttConfig.port, mqttConfig.user);
  }

  return success;
}

bool saveWallboxTopic(const char* topic) {
  if (!topic || strlen(topic) == 0) {
    return false;
  }

  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for writing wallbox topic");
    return false;
  }

  bool success = preferences.putString(NVS_KEY_WB_TOPIC, topic);

  if (success) {
    strncpy(mqttConfig.wallboxTopic, topic, sizeof(mqttConfig.wallboxTopic) - 1);
    mqttConfig.wallboxTopic[sizeof(mqttConfig.wallboxTopic) - 1] = '\0';
    Serial.printf("Wallbox topic saved: %s\n", mqttConfig.wallboxTopic);
  }

  preferences.end();
  return success;
}

bool saveLogLevel(uint8_t level) {
  if (level > LOG_LEVEL_ERROR) {
    return false;
  }

  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for writing log level");
    return false;
  }

  bool success = preferences.putUChar(NVS_KEY_LOG_LEVEL, level);

  if (success) {
    mqttConfig.logLevel = level;
    Serial.printf("Log level saved: %d\n", mqttConfig.logLevel);
  }

  preferences.end();
  return success;
}

bool resetToDefaults() {
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for clearing");
    return false;
  }

  bool success = preferences.clear();
  preferences.end();

  if (success) {
    getDefaultConfig(mqttConfig);
    Serial.println("Config reset to defaults");
  }

  return success;
}

// Boot count functions
uint8_t getBootCount() {
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    return 0;
  }
  uint8_t count = preferences.getUChar(NVS_KEY_BOOT_COUNT, 0);
  preferences.end();
  return count;
}

void incrementBootCount() {
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for boot count increment");
    return;
  }
  uint8_t count = preferences.getUChar(NVS_KEY_BOOT_COUNT, 0);
  count++;
  preferences.putUChar(NVS_KEY_BOOT_COUNT, count);
  preferences.end();
  Serial.printf("Boot count incremented to: %d\n", count);
}

void resetBootCount() {
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for boot count reset");
    return;
  }
  preferences.putUChar(NVS_KEY_BOOT_COUNT, 0);
  preferences.end();
  Serial.println("Boot count reset to 0");
}

// WiFi credentials functions
bool saveWiFiCredentials(const char* ssid, const char* pass) {
  if (!ssid || strlen(ssid) == 0) {
    return false;
  }

  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for writing WiFi credentials");
    return false;
  }

  bool success = true;
  success &= preferences.putString(NVS_KEY_WIFI_SSID, ssid);
  if (pass) {
    success &= preferences.putString(NVS_KEY_WIFI_PASS, pass);
  } else {
    success &= preferences.putString(NVS_KEY_WIFI_PASS, "");
  }

  preferences.end();

  if (success) {
    Serial.printf("WiFi credentials saved: SSID=%s\n", ssid);
  }

  return success;
}

bool loadWiFiCredentials(char* ssid, size_t ssidLen, char* pass, size_t passLen) {
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    return false;
  }

  String storedSSID = preferences.getString(NVS_KEY_WIFI_SSID, "");
  String storedPass = preferences.getString(NVS_KEY_WIFI_PASS, "");
  preferences.end();

  if (storedSSID.length() == 0) {
    return false;
  }

  strncpy(ssid, storedSSID.c_str(), ssidLen - 1);
  ssid[ssidLen - 1] = '\0';
  strncpy(pass, storedPass.c_str(), passLen - 1);
  pass[passLen - 1] = '\0';

  Serial.printf("WiFi credentials loaded: SSID=%s\n", ssid);
  return true;
}

bool hasStoredWiFiCredentials() {
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    return false;
  }

  String storedSSID = preferences.getString(NVS_KEY_WIFI_SSID, "");
  preferences.end();

  return storedSSID.length() > 0;
}

// Debug mode functions
bool isDebugModeEnabled() {
  if (!preferences.begin(NVS_NAMESPACE, true)) {
    return false;
  }
  bool enabled = preferences.getBool(NVS_KEY_DEBUG_MODE, false);
  preferences.end();
  return enabled;
}

void setDebugMode(bool enabled) {
  if (!preferences.begin(NVS_NAMESPACE, false)) {
    Serial.println("Failed to open NVS for debug mode");
    return;
  }
  preferences.putBool(NVS_KEY_DEBUG_MODE, enabled);
  preferences.end();
  Serial.printf("Debug mode %s\n", enabled ? "enabled" : "disabled");
}
