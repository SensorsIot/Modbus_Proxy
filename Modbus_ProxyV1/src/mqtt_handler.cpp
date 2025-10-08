#include "mqtt_handler.h"
#include "credentials.h"
#include "debug.h"
#include "evcc_api.h"
#include "modbus_proxy.h"

// Global MQTT objects
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
MQTTDataQueue mqttDataQueue = {NULL, {}, 0, 0, 0};
SystemHealth systemHealth = {0};
uint32_t mqttReconnectCount = 0;

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
  DEBUG_PRINTF("🔗 Setting up MQTT connection to %s:%d\n", mqttServer, mqttPort);

  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(60);
  mqttClient.setSocketTimeout(15);
  mqttClient.setCallback(onMqttMessage);

  mqttPublishQueue = xQueueCreate(10, sizeof(MQTTPublishItem));
  if (mqttPublishQueue == NULL) {
    DEBUG_PRINTLN("❌ Failed to create MQTT publish queue");
    return false;
  }

  mqttDataQueue.mutex = xSemaphoreCreateMutex();
  if (mqttDataQueue.mutex == NULL) {
    DEBUG_PRINTLN("❌ Failed to create MQTT data queue mutex");
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

  DEBUG_PRINTF("🔌 MQTT reconnection attempt #%lu...", mqttReconnectCount);

  String clientId = "MBUS_PROXY_" + WiFi.macAddress();
  clientId.replace(":", "");
  if (mqttClient.connect(clientId.c_str(), NULL, NULL, NULL, 0, false, NULL, true)) {
    DEBUG_PRINTLN(" ✅ CONNECTED!");
    return true;
  } else {
    DEBUG_PRINTF(" ❌ FAILED (state=%d)\n", mqttClient.state());
    return false;
  }
}

void mqttTask(void *pvParameters) {
  (void)pvParameters;
  uint32_t lastReportTime = 0;
  uint32_t lastDebugTime = 0;
  uint32_t lastEvccPoll = 0;
  uint32_t loopCount = 0;

  DEBUG_PRINTLN("📡 MQTT TASK STARTED");
  DEBUG_PRINTF("🧠 Task Info: Core=%d, Priority=%d\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));

  while (1) {
    uint32_t loopStart = millis();
    loopCount++;

    // Debug output every 10 seconds
    if (millis() - lastDebugTime > 10000) {
      uint32_t freeHeap = ESP.getFreeHeap();
      uint32_t minFreeHeap = ESP.getMinFreeHeap();
      DEBUG_PRINTF("🐛 MQTT Task Debug: Loop #%lu, Connected=%s, Heap=%u (min=%u)\n",
                    loopCount, mqttClient.connected() ? "YES" : "NO", freeHeap, minFreeHeap);
      lastDebugTime = millis();
    }

    // Update heartbeat for watchdog
    updateTaskHealthbeat(false);
    systemHealth.lastHealthReport = millis();

    // MQTT connection handling
    if (!mqttClient.connected()) {
      DEBUG_PRINTF("🔄 MQTT reconnecting... (state=%d)\n", mqttClient.state());
      if (!connectToMQTT()) {
        DEBUG_PRINTLN("⚠️  MQTT connection failed, continuing anyway");
      }
    }

    // Process MQTT loop
    uint32_t loopStartTime = millis();
    mqttClient.loop();
    uint32_t loopDuration = millis() - loopStartTime;
    if (loopDuration > 1000) {
      DEBUG_PRINTF("⚠️  MQTT loop took %lu ms\n", loopDuration);
    }

    // Process MQTT queue
    uint32_t queueStartTime = millis();
    processMQTTQueue();
    uint32_t queueDuration = millis() - queueStartTime;
    if (queueDuration > 1000) {
      DEBUG_PRINTF("⚠️  MQTT queue processing took %lu ms\n", queueDuration);
    }

    // EVCC API polling
    if (millis() - lastEvccPoll > HTTP_POLL_INTERVAL) {
      DEBUG_PRINTLN("🌐 Polling EVCC API...");
      uint32_t pollStart = millis();
      bool success = pollEvccApi(sharedEVCC);
      uint32_t pollDuration = millis() - pollStart;

      DEBUG_PRINTF("🌐 EVCC API poll %s in %lu ms\n",
                    success ? "SUCCESS" : "FAILED", pollDuration);

      if (pollDuration > 5000) {
        DEBUG_PRINTF("⚠️  EVCC API poll took %lu ms (blocking!)\n", pollDuration);
      }

      lastEvccPoll = millis();
    }

    // System health reporting
    if (millis() - lastReportTime > 60000) {
      lastReportTime = millis();
      DEBUG_PRINTLN("📊 Publishing system health...");
      publishSystemHealth(systemHealth);
    }

    uint32_t totalLoopTime = millis() - loopStart;
    if (totalLoopTime > 5000) {
      DEBUG_PRINTF("⚠️  MQTT task loop took %lu ms (too long!)\n", totalLoopTime);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  // Currently not subscribing to any MQTT topics
  // All data comes from EVCC HTTP API
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
  doc["evcc_updates"] = health.evccUpdates;
  doc["evcc_errors"] = health.evccErrors;
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
    DEBUG_PRINTLN("⚠️ MQTT publish queue full, dropping data");
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
      // Only report MQTT failures, not successes
      if (!success) {
        DEBUG_PRINTF("❌ MQTT publish FAILED (state=%d)\n", mqttClient.state());
      }
    } else {
      DEBUG_PRINTLN("⚠️ MQTT not connected, dropping queued data");
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
  DEBUG_PRINTF("🔍 MQTT Data: %s [%s]\n", time.c_str(), smid.c_str());
  DEBUG_PRINTF("   Power: %.1fW (L1:%.1f L2:%.1f L3:%.1f)\n",
                data.power_total, data.power_L1, data.power_L2, data.power_L3);
  DEBUG_PRINTF("   Voltage: %.1fV (L1:%.1f L2:%.1f L3:%.1f)\n",
                data.voltage_LN_avg, data.voltage_L1N, data.voltage_L2N, data.voltage_L3N);
  DEBUG_PRINTF("   Current: %.2fA (L1:%.2f L2:%.2f L3:%.2f)\n",
                (data.current_L1 + data.current_L2 + data.current_L3) / 3.0f,
                data.current_L1, data.current_L2, data.current_L3);
  DEBUG_PRINTF("   Frequency: %.2fHz\n", data.frequency);
}