//
// ESP32 MODBUS RTU Intelligent Proxy with Power Correction - ESP32-S3 Version
// ==========================================================================
//
// Modular architecture with separated concerns for improved maintainability

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ModbusRTU485.h"
#include <WiFi.h>
#include "credentials.h"

#include "config.h"
#include "dtsu666.h"
#include "evcc_api.h"
#include "mqtt_handler.h"
#include "modbus_proxy.h"
#include <ArduinoOTA.h>

// Global variable definitions are in credentials.h

// Output options
#ifndef PRINT_FANCY_TABLE
#define PRINT_FANCY_TABLE false
#endif

// WiFi and MQTT setup functions
void setupWiFi();
void setupMQTT();

void setupWiFi() {
  Serial.print("Connecting to WiFi");

  // Disable WiFi power saving to prevent spinlock issues
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("Modbus-Proxy");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.printf("WiFi connected! IP address: %s\n", WiFi.localIP().toString().c_str());
  Serial.println("✅ WiFi power saving disabled for stability");
}

void setupMQTT() {
  initMQTT();
}

void setupOTA() {
  ArduinoOTA.setHostname("Modbus-Proxy");
  ArduinoOTA.setPassword("modbus_ota_2023");

  ArduinoOTA.onStart([]() {
    Serial.println("🔄 OTA Update starting...");
    digitalWrite(STATUS_LED_PIN, HIGH);
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("✅ OTA Update completed");
    digitalWrite(STATUS_LED_PIN, LOW);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percentage = (progress * 100) / total;
    Serial.printf("📊 OTA Progress: %u%%\n", percentage);
    digitalWrite(STATUS_LED_PIN, (percentage % 20) < 10); // Blink during update
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("❌ OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");

    // Flash LED rapidly on error
    for (int i = 0; i < 10; i++) {
      digitalWrite(STATUS_LED_PIN, HIGH);
      delay(100);
      digitalWrite(STATUS_LED_PIN, LOW);
      delay(100);
    }
  });

  ArduinoOTA.begin();
  Serial.println("🔗 Arduino OTA Ready");
}

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(10);
    }
    Serial.println("🚀 ESP32-S3 MODBUS PROXY starting...");
    Serial.printf("📅 Build: %s %s\n", __DATE__, __TIME__);
    Serial.println("🎯 Mode: Modular ESP32-S3 proxy with configurable GPIO pins");

    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    // Startup LED sequence - blink 5 times
    Serial.println("💡 Status LED startup sequence");
    for (int i = 0; i < 5; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH);
        delay(200);
        digitalWrite(STATUS_LED_PIN, LOW);
        delay(200);
    }

    // Initialize system health monitoring
    uint32_t currentTime = millis();
    systemHealth.uptime = currentTime;
    systemHealth.freeHeap = ESP.getFreeHeap();
    systemHealth.minFreeHeap = ESP.getMinFreeHeap();
    Serial.println("🏥 System health monitoring initialized");

    // Initialize WiFi and MQTT
    setupWiFi();
    setupOTA();  // Add OTA after WiFi
    setupMQTT();

    // Initialize modular components
    if (!initModbusProxy()) {
        Serial.println("❌ Failed to initialize MODBUS proxy");
        ESP.restart();
    }

    if (!initEVCCAPI()) {
        Serial.println("❌ Failed to initialize EVCC API");
        ESP.restart();
    }


    // Create MQTT task (Core 0, lowest priority) - increased stack for EVCC API + JSON
    xTaskCreatePinnedToCore(
        mqttTask,
        "MQTTTask",
        16384, // Increased to 16KB for HTTP + large JSON buffer
        NULL,
        1,
        NULL,
        0
    );
    Serial.println("   ✅ MQTT task created (Core 0, Priority 1)");

    // Create proxy task (Core 1, medium priority)
    xTaskCreatePinnedToCore(
        proxyTask,
        "ProxyTask",
        4096,
        NULL,
        2,
        NULL,
        1
    );
    Serial.println("   ✅ Proxy task created (Core 1, Priority 2)");

    // Create watchdog task (Core 0, highest priority)
    xTaskCreatePinnedToCore(
        watchdogTask,
        "WatchdogTask",
        2048,
        NULL,
        3,
        NULL,
        0
    );
    Serial.println("   ✅ Watchdog task created (Core 0, Priority 3)");

    Serial.println("🔗 Modular ESP32-S3 proxy initialized!");
    Serial.println("   📡 MQTT publishing and EVCC API polling");
    Serial.println("   🔄 MODBUS proxy with power correction");
    Serial.println("   🐕 Independent health monitoring");
    Serial.println("⚡ Ready for operations!");
}

void loop() {
    ArduinoOTA.handle();
    vTaskDelay(1000);
}