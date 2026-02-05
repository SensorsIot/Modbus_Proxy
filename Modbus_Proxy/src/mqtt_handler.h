#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "dtsu666.h"

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

// Thread-safe shared MQTT received data structure
struct SharedMQTTReceivedData {
  SemaphoreHandle_t semaphore;          // Semaphore for thread-safe access
  MQTTSensorData data;                  // The MQTT sensor data
  uint32_t timestamp;                   // When data was last updated
  bool dataValid;                       // Whether data is valid
  bool newPackageArrived;               // Flag for new package arrival
};

// MQTT data queue structure
struct MQTTDataQueue {
  SemaphoreHandle_t mutex;
  static const size_t QUEUE_SIZE = 10;
  struct QueueItem {
    MQTTSensorData data;
    uint32_t timestamp;
    bool valid;
  } items[QUEUE_SIZE];
  size_t head;
  size_t tail;
  size_t count;
};

// System health structure
struct SystemHealth {
  uint32_t uptime;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t mqttReconnects;
  uint32_t dtsuUpdates;
  uint32_t wallboxUpdates;
  uint32_t wallboxErrors;
  uint32_t proxyErrors;
  float lastPowerCorrection;
  bool powerCorrectionActive;
  uint32_t lastHealthReport;
};

// Function declarations
bool initMQTT();
bool connectToMQTT();
void mqttTask(void *pvParameters);
void onMqttMessage(char* topic, byte* payload, unsigned int length);

// Topic subscription and message handling
void subscribeToTopics();
void handleWallboxPower(byte* payload, unsigned int length);
void handleConfigCommand(byte* payload, unsigned int length);
void triggerMqttReconnect();

// Data publishing functions
bool publishDTSUData(const DTSU666Data& data);
bool publishSystemHealth(const SystemHealth& health);
bool publishPowerData(const DTSU666Data& dtsuData, float correction, bool correctionActive);
void queueCorrectedPowerData(const DTSU666Data& finalData, const DTSU666Data& originalData,
                            bool correctionApplied, float correction);

// Log queue processing
void processLogQueue();

// MQTT utilities
bool mqttPublish(const char* topic, const char* payload, bool retained = false);
bool mqttPublishJSON(const char* topic, const JsonDocument& doc, bool retained = false);
void processMQTTQueue();

// Data conversion functions
void convertDTSUToMQTT(const DTSU666Data& dtsu, MQTTSensorData& mqtt, const String& time, const String& smid);

// Debug functions
void debugMQTTData(const String& time, const String& smid, const DTSU666Data& data);

// Global MQTT objects (declared extern to be defined in mqtt_handler.cpp)
extern WiFiClient wifiClient;
extern PubSubClient mqttClient;
extern MQTTDataQueue mqttDataQueue;
extern SystemHealth systemHealth;
extern uint32_t mqttReconnectCount;
extern bool mqttReconnectRequested;
