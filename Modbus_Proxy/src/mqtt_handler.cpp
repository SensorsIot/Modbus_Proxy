#include "mqtt_handler.h"
#include "credentials.h"
#include "debug.h"
#include "wallbox_data.h"
#include "modbus_proxy.h"
#include "nvs_config.h"
#include "mqtt_logger.h"

// Global MQTT objects
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
MQTTDataQueue mqttDataQueue = {NULL, {}, 0, 0, 0};
SystemHealth systemHealth = {0};
uint32_t mqttReconnectCount = 0;
bool mqttReconnectRequested = false;

// MQTT publish queue
QueueHandle_t mqttPublishQueue;

struct MQTTPublishItem {
  DTSU666Data correctedData;
  DTSU666Data originalData;
  bool correctionApplied;
  float correctionValue;
  uint32_t timestamp;
};

bool initMQTT() {
  DEBUG_PRINTF("Setting up MQTT connection to %s:%d\n", mqttConfig.host, mqttConfig.port);

  mqttClient.setServer(mqttConfig.host, mqttConfig.port);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);
  mqttClient.setCallback(onMqttMessage);

  mqttPublishQueue = xQueueCreate(10, sizeof(MQTTPublishItem));
  if (mqttPublishQueue == NULL) {
    DEBUG_PRINTLN("Failed to create MQTT publish queue");
    return false;
  }

  mqttDataQueue.mutex = xSemaphoreCreateMutex();
  if (mqttDataQueue.mutex == NULL) {
    DEBUG_PRINTLN("Failed to create MQTT data queue mutex");
    return false;
  }

  return true;
}

bool connectToMQTT() {
  if (mqttClient.connected()) {
    return true;
  }

  mqttReconnectCount++;
  systemHealth.mqttReconnects = mqttReconnectCount;

  DEBUG_PRINTF("MQTT reconnection attempt #%lu to %s:%d...", mqttReconnectCount, mqttConfig.host, mqttConfig.port);

  String clientId = "MBUS_PROXY_" + WiFi.macAddress();
  clientId.replace(":", "");

  bool connected = false;
  if (strlen(mqttConfig.user) > 0) {
    connected = mqttClient.connect(clientId.c_str(), mqttConfig.user, mqttConfig.pass, NULL, 0, false, NULL, true);
  } else {
    connected = mqttClient.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true);
  }

  if (connected) {
    DEBUG_PRINTLN(" CONNECTED!");
    MLOG_INFO("MQTT", "Connected to %s:%d (attempt #%lu)", mqttConfig.host, mqttConfig.port, mqttReconnectCount);
    subscribeToTopics();
    mqttLoggerConnected = true;
    return true;
  } else {
    DEBUG_PRINTF(" FAILED (state=%d)\n", mqttClient.state());
    MLOG_WARN("MQTT", "Connection failed to %s:%d (state=%d)", mqttConfig.host, mqttConfig.port, mqttClient.state());
    mqttLoggerConnected = false;
    return false;
  }
}

void subscribeToTopics() {
  // Subscribe to wallbox power topic
  if (mqttClient.subscribe(mqttConfig.wallboxTopic)) {
    DEBUG_PRINTF("Subscribed to wallbox topic: %s\n", mqttConfig.wallboxTopic);
    MLOG_INFO("MQTT", "Subscribed to: %s", mqttConfig.wallboxTopic);
  } else {
    DEBUG_PRINTF("Failed to subscribe to: %s\n", mqttConfig.wallboxTopic);
    MLOG_ERROR("MQTT", "Subscribe failed: %s", mqttConfig.wallboxTopic);
  }

  // Subscribe to config command topic
  if (mqttClient.subscribe(MQTT_TOPIC_CMD_CONFIG)) {
    DEBUG_PRINTF("Subscribed to config topic: %s\n", MQTT_TOPIC_CMD_CONFIG);
    MLOG_INFO("MQTT", "Subscribed to: %s", MQTT_TOPIC_CMD_CONFIG);
  } else {
    DEBUG_PRINTF("Failed to subscribe to: %s\n", MQTT_TOPIC_CMD_CONFIG);
    MLOG_ERROR("MQTT", "Subscribe failed: %s", MQTT_TOPIC_CMD_CONFIG);
  }
}

void triggerMqttReconnect() {
  mqttReconnectRequested = true;
  DEBUG_PRINTLN("MQTT reconnect requested (config changed)");
}

void mqttTask(void *pvParameters) {
  (void)pvParameters;
  uint32_t lastReportTime = 0;
  uint32_t lastDebugTime = 0;
  uint32_t loopCount = 0;

  DEBUG_PRINTLN("MQTT TASK STARTED");
  DEBUG_PRINTF("Task Info: Core=%d, Priority=%d\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));

  while (1) {
    uint32_t loopStart = millis();
    loopCount++;

    // Debug output every 10 seconds
    if (millis() - lastDebugTime > 10000) {
      uint32_t freeHeap = ESP.getFreeHeap();
      uint32_t minFreeHeap = ESP.getMinFreeHeap();
      DEBUG_PRINTF("MQTT Task Debug: Loop #%lu, Connected=%s, Heap=%u (min=%u)\n",
                    loopCount, mqttClient.connected() ? "YES" : "NO", freeHeap, minFreeHeap);
      lastDebugTime = millis();
    }

    // Update heartbeat for watchdog
    updateTaskHealthbeat(false);
    systemHealth.lastHealthReport = millis();

    // Handle reconnect request (from config change)
    if (mqttReconnectRequested) {
      mqttReconnectRequested = false;
      mqttClient.disconnect();
      delay(100);
      mqttClient.setServer(mqttConfig.host, mqttConfig.port);
      DEBUG_PRINTF("MQTT server updated to %s:%d\n", mqttConfig.host, mqttConfig.port);
    }

    // MQTT connection handling
    static bool wasConnected = false;
    if (!mqttClient.connected()) {
      if (wasConnected) {
        MLOG_WARN("MQTT", "Connection lost (state=%d)", mqttClient.state());
        wasConnected = false;
      }
      mqttLoggerConnected = false;
      DEBUG_PRINTF("MQTT reconnecting... (state=%d)\n", mqttClient.state());
      if (connectToMQTT()) {
        wasConnected = true;
      } else {
        DEBUG_PRINTLN("MQTT connection failed, continuing anyway");
      }
    } else {
      wasConnected = true;
    }

    // Process MQTT loop
    uint32_t loopStartTime = millis();
    mqttClient.loop();
    uint32_t loopDuration = millis() - loopStartTime;
    if (loopDuration > 1000) {
      DEBUG_PRINTF("MQTT loop took %lu ms\n", loopDuration);
    }

    // Process MQTT queue
    uint32_t queueStartTime = millis();
    processMQTTQueue();
    uint32_t queueDuration = millis() - queueStartTime;
    if (queueDuration > 1000) {
      DEBUG_PRINTF("MQTT queue processing took %lu ms\n", queueDuration);
    }

    // Process log queue (send buffered logs via MQTT)
    processLogQueue();

    // System health reporting
    if (millis() - lastReportTime > 60000) {
      lastReportTime = millis();
      DEBUG_PRINTLN("Publishing system health...");
      publishSystemHealth(systemHealth);
    }

    uint32_t totalLoopTime = millis() - loopStart;
    if (totalLoopTime > 5000) {
      DEBUG_PRINTF("MQTT task loop took %lu ms (too long!)\n", totalLoopTime);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  // Route message to appropriate handler
  if (strcmp(topic, mqttConfig.wallboxTopic) == 0) {
    handleWallboxPower(payload, length);
  } else if (strcmp(topic, MQTT_TOPIC_CMD_CONFIG) == 0) {
    handleConfigCommand(payload, length);
  }
}

void handleWallboxPower(byte* payload, unsigned int length) {
  // Validate message size
  if (length == 0) {
    systemHealth.wallboxErrors++;
    MLOG_WARN("MQTT", "Empty wallbox message received");
    return;
  }

  if (length > 256) {
    systemHealth.wallboxErrors++;
    MLOG_WARN("MQTT", "Oversized wallbox message (%u bytes > 256)", length);
    return;
  }

  // Null-terminate the payload
  char buffer[257];
  memcpy(buffer, payload, length);
  buffer[length] = '\0';

  float power = 0.0f;
  bool parsed = false;

  // Try parsing as plain float first
  char* endptr;
  float val = strtof(buffer, &endptr);
  if (endptr != buffer && (*endptr == '\0' || *endptr == '\n' || *endptr == '\r')) {
    power = val;
    parsed = true;
  }

  // Try parsing as JSON if plain float failed
  if (!parsed) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, buffer);

    if (!error) {
      // Try "power" key first
      if (doc.containsKey("power")) {
        power = doc["power"].as<float>();
        parsed = true;
      }
      // Try "chargePower" key (EVCC compatibility)
      else if (doc.containsKey("chargePower")) {
        power = doc["chargePower"].as<float>();
        parsed = true;
      }
    }
  }

  if (parsed) {
    updateWallboxPower(power);
    systemHealth.wallboxUpdates++;
    DEBUG_PRINTF("Wallbox power updated: %.1fW\n", power);
  } else {
    systemHealth.wallboxErrors++;
    MLOG_WARN("MQTT", "Failed to parse wallbox power: %.32s%s", buffer, length > 32 ? "..." : "");
  }
}

void handleConfigCommand(byte* payload, unsigned int length) {
  if (length == 0) {
    MLOG_WARN("CONFIG", "Empty config command received");
    return;
  }
  if (length > 512) {
    MLOG_WARN("CONFIG", "Oversized config command (%u bytes > 512)", length);
    return;
  }

  char buffer[513];
  memcpy(buffer, payload, length);
  buffer[length] = '\0';

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, buffer);

  if (error) {
    DEBUG_PRINTF("Config command parse error: %s\n", error.c_str());
    MLOG_WARN("CONFIG", "JSON parse error: %s", error.c_str());
    return;
  }

  if (!doc.containsKey("cmd")) {
    DEBUG_PRINTLN("Config command missing 'cmd' field");
    MLOG_WARN("CONFIG", "Missing 'cmd' field in command");
    return;
  }

  const char* cmd = doc["cmd"];
  StaticJsonDocument<256> response;
  response["cmd"] = cmd;

  if (strcmp(cmd, "set_mqtt") == 0) {
    const char* host = doc["host"] | "";
    uint16_t port = doc["port"] | 0;
    const char* user = doc["user"] | "";
    const char* pass = doc["pass"] | "";

    if (saveMQTTCredentials(host, port, user, pass)) {
      response["status"] = "ok";
      response["message"] = "MQTT credentials saved, reconnecting...";
      MLOG_INFO("CONFIG", "MQTT credentials updated: %s:%d user=%s", host, port, user);
      triggerMqttReconnect();
    } else {
      response["status"] = "error";
      response["message"] = "Failed to save MQTT credentials";
      MLOG_ERROR("CONFIG", "Failed to save MQTT credentials");
    }
  }
  else if (strcmp(cmd, "set_wallbox_topic") == 0) {
    const char* topic = doc["topic"] | "";
    if (strlen(topic) > 0 && saveWallboxTopic(topic)) {
      response["status"] = "ok";
      response["message"] = "Wallbox topic saved, reconnecting...";
      MLOG_INFO("CONFIG", "Wallbox topic changed to: %s", topic);
      triggerMqttReconnect();
    } else {
      response["status"] = "error";
      response["message"] = "Failed to save wallbox topic";
      MLOG_WARN("CONFIG", "Failed to save wallbox topic: %s", topic);
    }
  }
  else if (strcmp(cmd, "set_log_level") == 0) {
    uint8_t level = doc["level"] | 255;
    if (level <= LOG_LEVEL_ERROR && saveLogLevel(level)) {
      response["status"] = "ok";
      response["level"] = level;
      MLOG_INFO("CONFIG", "Log level changed to %d (%s)", level, LOG_LEVEL_NAMES[level]);
    } else {
      response["status"] = "error";
      response["message"] = "Invalid log level (0-3)";
      MLOG_WARN("CONFIG", "Invalid log level: %d", level);
    }
  }
  else if (strcmp(cmd, "get_config") == 0) {
    response["status"] = "ok";
    response["mqtt_host"] = mqttConfig.host;
    response["mqtt_port"] = mqttConfig.port;
    response["mqtt_user"] = mqttConfig.user;
    response["wallbox_topic"] = mqttConfig.wallboxTopic;
    response["log_level"] = mqttConfig.logLevel;
    MLOG_DEBUG("CONFIG", "Config requested via MQTT");
  }
  else if (strcmp(cmd, "factory_reset") == 0) {
    MLOG_WARN("CONFIG", "Factory reset requested!");
    if (resetToDefaults()) {
      response["status"] = "ok";
      response["message"] = "Config reset to defaults, reconnecting...";
      MLOG_INFO("CONFIG", "Factory reset completed");
      triggerMqttReconnect();
    } else {
      response["status"] = "error";
      response["message"] = "Failed to reset config";
      MLOG_ERROR("CONFIG", "Factory reset failed");
    }
  }
  else {
    response["status"] = "error";
    response["message"] = "Unknown command";
    MLOG_WARN("CONFIG", "Unknown command: %s", cmd);
  }

  // Publish response
  mqttPublishJSON(MQTT_TOPIC_CMD_RESPONSE, response);
}

void processLogQueue() {
  if (!mqttClient.connected()) {
    return;
  }

  // Process up to 3 log entries per call to avoid blocking
  for (int i = 0; i < 3; i++) {
    LogEntry entry;
    if (!getNextLogEntry(entry)) {
      break;
    }

    StaticJsonDocument<256> doc;
    doc["ts"] = entry.timestamp;
    doc["level"] = LOG_LEVEL_NAMES[entry.level];
    doc["subsys"] = entry.subsystem;
    doc["msg"] = entry.message;

    mqttPublishJSON(MQTT_TOPIC_LOG, doc);
  }
}

bool publishDTSUData(const DTSU666Data& data) {
  if (!mqttClient.connected()) {
    return false;
  }

  StaticJsonDocument<1024> doc;
  doc["timestamp"] = millis();
  doc["device"] = "ESP32_ModbusProxy";
  doc["source"] = "DTSU-666";

  doc["power_total"] = data.power_total;
  doc["power_L1"] = data.power_L1;
  doc["power_L2"] = data.power_L2;
  doc["power_L3"] = data.power_L3;

  doc["voltage_L1N"] = data.voltage_L1N;
  doc["voltage_L2N"] = data.voltage_L2N;
  doc["voltage_L3N"] = data.voltage_L3N;

  doc["current_L1"] = data.current_L1;
  doc["current_L2"] = data.current_L2;
  doc["current_L3"] = data.current_L3;

  doc["frequency"] = data.frequency;

  return mqttPublishJSON(MQTT_TOPIC_DTSU, doc);
}

bool publishSystemHealth(const SystemHealth& health) {
  if (!mqttClient.connected()) {
    return false;
  }

  StaticJsonDocument<512> doc;
  doc["timestamp"] = millis();
  doc["uptime"] = health.uptime;
  doc["free_heap"] = health.freeHeap;
  doc["min_free_heap"] = health.minFreeHeap;
  doc["mqtt_reconnects"] = health.mqttReconnects;
  doc["dtsu_updates"] = health.dtsuUpdates;
  doc["wallbox_updates"] = health.wallboxUpdates;
  doc["wallbox_errors"] = health.wallboxErrors;
  doc["proxy_errors"] = health.proxyErrors;
  doc["power_correction"] = health.lastPowerCorrection;
  doc["correction_active"] = health.powerCorrectionActive;

  return mqttPublishJSON(MQTT_TOPIC_HEALTH, doc);
}

bool publishPowerData(const DTSU666Data& dtsuData, float correction, bool correctionActive) {
  if (!mqttClient.connected()) {
    return false;
  }

  StaticJsonDocument<512> doc;
  doc["timestamp"] = millis();
  doc["dtsu_power"] = dtsuData.power_total;
  doc["correction"] = correction;
  doc["correction_active"] = correctionActive;
  doc["sun2000_power"] = dtsuData.power_total + (correctionActive ? correction : 0.0f);

  return mqttPublishJSON(MQTT_TOPIC_STATUS, doc);
}

void queueCorrectedPowerData(const DTSU666Data& finalData, const DTSU666Data& originalData,
                            bool correctionApplied, float correction) {
  MQTTPublishItem item;
  item.correctedData = finalData;
  item.originalData = originalData;
  item.correctionApplied = correctionApplied;
  item.correctionValue = correction;
  item.timestamp = millis();

  if (xQueueSend(mqttPublishQueue, &item, 0) != pdTRUE) {
    DEBUG_PRINTLN("MQTT publish queue full, dropping data");
  }
}

bool mqttPublish(const char* topic, const char* payload, bool retained) {
  if (!mqttClient.connected()) {
    return false;
  }

  return mqttClient.publish(topic, payload, retained);
}

bool mqttPublishJSON(const char* topic, const JsonDocument& doc, bool retained) {
  String jsonString;
  serializeJson(doc, jsonString);
  return mqttPublish(topic, jsonString.c_str(), retained);
}

void processMQTTQueue() {
  MQTTPublishItem item;
  if (xQueueReceive(mqttPublishQueue, &item, 10 / portTICK_PERIOD_MS) == pdTRUE) {
    if (mqttClient.connected()) {
      StaticJsonDocument<256> doc;

      doc["dtsu"] = item.originalData.power_total;
      doc["wallbox"] = item.correctionValue;
      doc["sun2000"] = item.correctedData.power_total;
      doc["active"] = item.correctionApplied;

      bool success = mqttPublishJSON(MQTT_TOPIC_POWER, doc);
      if (!success) {
        DEBUG_PRINTF("MQTT publish FAILED (state=%d)\n", mqttClient.state());
      }
    } else {
      DEBUG_PRINTLN("MQTT not connected, dropping queued data");
    }
  }
}

void convertDTSUToMQTT(const DTSU666Data& dtsu, MQTTSensorData& mqtt, const String& time, const String& smid) {
  mqtt.time = time;
  mqtt.smid = smid;

  mqtt.pi = dtsu.power_total > 0 ? dtsu.power_total / 1000.0f : 0.0f;
  mqtt.po = dtsu.power_total < 0 ? -dtsu.power_total / 1000.0f : 0.0f;

  mqtt.pi1 = dtsu.power_L1 > 0 ? dtsu.power_L1 : 0.0f;
  mqtt.pi2 = dtsu.power_L2 > 0 ? dtsu.power_L2 : 0.0f;
  mqtt.pi3 = dtsu.power_L3 > 0 ? dtsu.power_L3 : 0.0f;

  mqtt.po1 = dtsu.power_L1 < 0 ? -dtsu.power_L1 : 0.0f;
  mqtt.po2 = dtsu.power_L2 < 0 ? -dtsu.power_L2 : 0.0f;
  mqtt.po3 = dtsu.power_L3 < 0 ? -dtsu.power_L3 : 0.0f;

  mqtt.u1 = dtsu.voltage_L1N;
  mqtt.u2 = dtsu.voltage_L2N;
  mqtt.u3 = dtsu.voltage_L3N;

  mqtt.i1 = dtsu.current_L1;
  mqtt.i2 = dtsu.current_L2;
  mqtt.i3 = dtsu.current_L3;

  mqtt.f = dtsu.frequency;

  mqtt.ei = dtsu.import_total;
  mqtt.eo = dtsu.export_total;
}

void debugMQTTData(const String& time, const String& smid, const DTSU666Data& data) {
  DEBUG_PRINTF("MQTT Data: %s [%s]\n", time.c_str(), smid.c_str());
  DEBUG_PRINTF("   Power: %.1fW (L1:%.1f L2:%.1f L3:%.1f)\n",
                data.power_total, data.power_L1, data.power_L2, data.power_L3);
  DEBUG_PRINTF("   Voltage: %.1fV (L1:%.1f L2:%.1f L3:%.1f)\n",
                data.voltage_LN_avg, data.voltage_L1N, data.voltage_L2N, data.voltage_L3N);
  DEBUG_PRINTF("   Current: %.2fA (L1:%.2f L2:%.2f L3:%.2f)\n",
                (data.current_L1 + data.current_L2 + data.current_L3) / 3.0f,
                data.current_L1, data.current_L2, data.current_L3);
  DEBUG_PRINTF("   Frequency: %.2fHz\n", data.frequency);
}
