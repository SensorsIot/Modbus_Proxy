#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ModbusRTU485.h"
#include <WiFi.h>
#include "credentials.h"

#include "config.h"
#include "debug.h"
#include "dtsu666.h"
#include "evcc_api.h"
#include "mqtt_handler.h"
#include "modbus_proxy.h"
#include <ArduinoOTA.h>

#if defined(ENABLE_TELNET_DEBUG) && ENABLE_TELNET_DEBUG
TelnetDebug telnetDebug;
#endif

void setupWiFi();
void setupMQTT();

void setupWiFi() {
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);

  DEBUG_PRINTF("📡 Connecting to WiFi SSID: %s\n", ssid);
  WiFi.setHostname("MODBUS-Proxy");
  WiFi.begin(ssid, password);

  uint32_t startTime = millis();
  const uint32_t WIFI_TIMEOUT = 30000;

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_TIMEOUT) {
      DEBUG_PRINTLN();
      DEBUG_PRINTF("❌ WiFi connection timeout - Final status: %d\n", WiFi.status());
      DEBUG_PRINTLN("Restarting...");
      ESP.restart();
    }

    LED_ON();
    delay(350);
    LED_OFF();
    delay(350);

    DEBUG_PRINTF("[%d]", WiFi.status());
  }

  DEBUG_PRINTLN();
  DEBUG_PRINTF("WiFi connected! IP address: %s\n", WiFi.localIP().toString().c_str());
  DEBUG_PRINTLN("✅ WiFi power saving disabled for stability");

  for (int i = 0; i < 2; i++) {
    LED_ON();
    delay(200);
    LED_OFF();
    delay(200);
  }
}

void setupMQTT() {
  initMQTT();
}

void setupOTA() {
  ArduinoOTA.setHostname("MODBUS-Proxy");
  ArduinoOTA.setPassword("modbus_ota_2023");

  ArduinoOTA.onStart([]() {
    DEBUG_PRINTLN("🔄 OTA Update starting...");
    LED_ON();
  });

  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("✅ OTA Update completed");
    LED_OFF();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percentage = (progress * 100) / total;
    DEBUG_PRINTF("📊 OTA Progress: %u%%\n", percentage);
    if ((percentage % 20) < 10) LED_ON(); else LED_OFF();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    DEBUG_PRINTF("❌ OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) DEBUG_PRINTLN("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) DEBUG_PRINTLN("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) DEBUG_PRINTLN("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) DEBUG_PRINTLN("Receive Failed");
    else if (error == OTA_END_ERROR) DEBUG_PRINTLN("End Failed");

    for (int i = 0; i < 10; i++) {
      LED_ON();
      delay(100);
      LED_OFF();
      delay(100);
    }
  });

  ArduinoOTA.begin();
  DEBUG_PRINTLN("🔗 Arduino OTA Ready");
}

void setup() {
#if ENABLE_SERIAL_DEBUG
    Serial.begin(115200);
    delay(100);
#endif

    pinMode(STATUS_LED_PIN, OUTPUT);
    LED_OFF();

    for (int i = 0; i < 5; i++) {
        LED_ON();
        delay(100);
        LED_OFF();
        delay(100);
    }

    DEBUG_PRINTLN("🚀 ESP32-C3 MODBUS PROXY starting...");
    DEBUG_PRINTF("📅 Build: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTLN("🎯 Mode: Modular ESP32-C3 single-core proxy with configurable GPIO pins");
    DEBUG_PRINTLN("\n⚙️  Configuration Parameters:");
    DEBUG_PRINTF("   WiFi SSID: '%s'\n", ssid);
    DEBUG_PRINTF("   WiFi Password: '%s'\n", password);
    DEBUG_PRINTF("   MQTT Server: %s:%d\n", mqttServer, mqttPort);
    DEBUG_PRINTF("   EVCC API URL: %s\n", evccApiUrl);
    DEBUG_PRINTF("   RS485 SUN2000: RX=%d, TX=%d\n", RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);
    DEBUG_PRINTF("   RS485 DTU: RX=%d, TX=%d\n", RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);
    DEBUG_PRINTF("   Status LED: GPIO %d\n", STATUS_LED_PIN);
    DEBUG_PRINTF("   MODBUS Baudrate: %d\n", MODBUS_BAUDRATE);
    DEBUG_PRINTF("   Power Correction Threshold: %.0f W\n", CORRECTION_THRESHOLD);
    DEBUG_PRINTF("   HTTP Poll Interval: %d ms\n", HTTP_POLL_INTERVAL);
    DEBUG_PRINTF("   Watchdog Timeout: %d ms\n", WATCHDOG_TIMEOUT_MS);
    DEBUG_PRINTF("   Serial Debug: %s\n\n", ENABLE_SERIAL_DEBUG ? "ENABLED" : "DISABLED");

    setupWiFi();
    setupOTA();

#if defined(ENABLE_TELNET_DEBUG) && ENABLE_TELNET_DEBUG
    telnetDebug.begin(23);
    DEBUG_PRINTLN("📡 Telnet debug server started on port 23");
    DEBUG_PRINTF("   Connect via: telnet %s 23\n", WiFi.localIP().toString().c_str());
#endif

    uint32_t currentTime = millis();
    systemHealth.uptime = currentTime;
    systemHealth.freeHeap = ESP.getFreeHeap();
    systemHealth.minFreeHeap = ESP.getMinFreeHeap();
    DEBUG_PRINTLN("🏥 System health monitoring initialized");

    setupMQTT();

    if (!initModbusProxy()) {
        DEBUG_PRINTLN("❌ Failed to initialize MODBUS proxy");
        ESP.restart();
    }

    if (!initEVCCAPI()) {
        DEBUG_PRINTLN("❌ Failed to initialize EVCC API");
        ESP.restart();
    }

    xTaskCreate(
        mqttTask,
        "MQTTTask",
        16384,
        NULL,
        1,
        NULL
    );
    DEBUG_PRINTLN("   ✅ MQTT task created (Priority 1)");

    xTaskCreate(
        proxyTask,
        "ProxyTask",
        4096,
        NULL,
        2,
        NULL
    );
    DEBUG_PRINTLN("   ✅ Proxy task created (Priority 2)");

    xTaskCreate(
        watchdogTask,
        "WatchdogTask",
        2048,
        NULL,
        3,
        NULL
    );
    DEBUG_PRINTLN("   ✅ Watchdog task created (Priority 3)");

    DEBUG_PRINTLN("🔗 Modular ESP32-C3 proxy initialized!");
    DEBUG_PRINTLN("   📡 MQTT publishing and EVCC API polling");
    DEBUG_PRINTLN("   🔄 MODBUS proxy with power correction");
    DEBUG_PRINTLN("   🐕 Independent health monitoring");
    DEBUG_PRINTLN("⚡ Ready for operations!");

    for (int i = 0; i < 5; i++) {
        LED_ON();
        delay(100);
        LED_OFF();
        delay(100);
    }
}

void loop() {
    ArduinoOTA.handle();
    DEBUG_HANDLE();
    vTaskDelay(100);
}