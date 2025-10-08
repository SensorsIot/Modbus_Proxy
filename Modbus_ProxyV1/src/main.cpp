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
#include <Adafruit_NeoPixel.h>

#include "config.h"
#include "debug.h"
#include "dtsu666.h"
#include "evcc_api.h"
#include "mqtt_handler.h"
#include "modbus_proxy.h"
#include <ArduinoOTA.h>

// NeoPixel setup
Adafruit_NeoPixel pixel(1, STATUS_LED_PIN, NEO_GRB + NEO_KHZ800);

// Global variable definitions are in credentials.h

// Output options
#ifndef PRINT_FANCY_TABLE
#define PRINT_FANCY_TABLE false
#endif

// WiFi and MQTT setup functions
void setupWiFi();
void setupMQTT();

void setupWiFi() {
  // Disable WiFi power saving to prevent spinlock issues
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);

  DEBUG_PRINTLN("📡 Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  DEBUG_PRINTF("Found %d networks:\n", n);

  int bestTargetIndex = -1;
  int bestRSSI = -999;

  for (int i = 0; i < n; i++) {
    String currentSSID = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    uint8_t* bssid = WiFi.BSSID(i);

    DEBUG_PRINTF("  %d: %s (%d dBm) %s [%02X:%02X:%02X:%02X:%02X:%02X]",
      i + 1,
      currentSSID.c_str(),
      rssi,
      WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "OPEN" : "ENCRYPTED",
      bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]
    );

    if (currentSSID == ssid) {
      DEBUG_PRINT(" ← TARGET");
      if (rssi > bestRSSI) {
        bestRSSI = rssi;
        bestTargetIndex = i;
      }
    }
    DEBUG_PRINTLN();
  }
  DEBUG_PRINTLN();

  if (bestTargetIndex == -1) {
    DEBUG_PRINTF("⚠️  WARNING: Target SSID '%s' not found in scan!\n\n", ssid);
    DEBUG_PRINTF("📡 Connecting to WiFi SSID: %s\n", ssid);
    WiFi.setHostname("MODBUS-Proxy");
    int status = WiFi.begin(ssid, password);
    DEBUG_PRINTF("WiFi.begin() returned: %d\n", status);
  } else {
    uint8_t* bestBSSID = WiFi.BSSID(bestTargetIndex);
    DEBUG_PRINTF("🎯 Best AP found: %s with %d dBm [%02X:%02X:%02X:%02X:%02X:%02X]\n",
      ssid, bestRSSI,
      bestBSSID[0], bestBSSID[1], bestBSSID[2], bestBSSID[3], bestBSSID[4], bestBSSID[5]
    );
    DEBUG_PRINTF("📡 Connecting to strongest AP...\n");
    WiFi.setHostname("MODBUS-Proxy");
    int status = WiFi.begin(ssid, password, 0, bestBSSID);
    DEBUG_PRINTF("WiFi.begin() with BSSID returned: %d\n", status);
  }

  uint32_t startTime = millis();
  const uint32_t WIFI_TIMEOUT = 30000;  // 30 second timeout

  // Phase 2: WiFi connection - Blink blue during attempts
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_TIMEOUT) {
      DEBUG_PRINTLN();
      DEBUG_PRINTF("❌ WiFi connection timeout - Final status: %d\n", WiFi.status());
      DEBUG_PRINTLN("Restarting...");
      ESP.restart();
    }

    // Blink blue for 700ms
    pixel.setPixelColor(0, pixel.Color(0, 0, 255));
    pixel.show();
    delay(350);
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
    delay(350);

    DEBUG_PRINTF("[%d]", WiFi.status());
  }

  DEBUG_PRINTLN();
  DEBUG_PRINTF("WiFi connected! IP address: %s\n", WiFi.localIP().toString().c_str());
  DEBUG_PRINTLN("✅ WiFi power saving disabled for stability");

  // Blink green 2 times to indicate WiFi connection success
  for (int i = 0; i < 2; i++) {
    pixel.setPixelColor(0, pixel.Color(0, 255, 0));
    pixel.show();
    delay(200);
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    pixel.show();
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
    pixel.setPixelColor(0, pixel.Color(255, 255, 0));  // Yellow
    pixel.show();
  });

  ArduinoOTA.onEnd([]() {
    DEBUG_PRINTLN("✅ OTA Update completed");
    pixel.setPixelColor(0, pixel.Color(0, 0, 0));  // Off
    pixel.show();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int percentage = (progress * 100) / total;
    DEBUG_PRINTF("📊 OTA Progress: %u%%\n", percentage);
    // Pulse cyan during update
    if ((percentage % 20) < 10) {
      pixel.setPixelColor(0, pixel.Color(0, 255, 255));
    } else {
      pixel.setPixelColor(0, pixel.Color(0, 0, 0));
    }
    pixel.show();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    DEBUG_PRINTF("❌ OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) DEBUG_PRINTLN("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) DEBUG_PRINTLN("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) DEBUG_PRINTLN("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) DEBUG_PRINTLN("Receive Failed");
    else if (error == OTA_END_ERROR) DEBUG_PRINTLN("End Failed");

    // Flash red rapidly on error
    for (int i = 0; i < 10; i++) {
      pixel.setPixelColor(0, pixel.Color(255, 0, 0));
      pixel.show();
      delay(100);
      pixel.setPixelColor(0, pixel.Color(0, 0, 0));
      pixel.show();
      delay(100);
    }
  });

  ArduinoOTA.begin();
  DEBUG_PRINTLN("🔗 Arduino OTA Ready");
}

void setup() {
#if ENABLE_SERIAL_DEBUG
    Serial.begin(115200);
#endif

    // Initialize NeoPixel
    pixel.begin();
    pixel.setBrightness(50);
    pixel.show();

    // Phase 1: Startup - Blink green 5 times
    for (int i = 0; i < 5; i++) {
        pixel.setPixelColor(0, pixel.Color(0, 255, 0));
        pixel.show();
        delay(100);
        pixel.setPixelColor(0, pixel.Color(0, 0, 0));
        pixel.show();
        delay(100);
    }

    DEBUG_PRINTLN("🚀 ESP32-S3 MODBUS PROXY starting...");
    DEBUG_PRINTF("📅 Build: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTLN("🎯 Mode: Modular ESP32-S3 proxy with configurable GPIO pins");
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

    // Initialize WiFi FIRST, before any other subsystems
    setupWiFi();
    setupOTA();

    // Initialize system health monitoring
    uint32_t currentTime = millis();
    systemHealth.uptime = currentTime;
    systemHealth.freeHeap = ESP.getFreeHeap();
    systemHealth.minFreeHeap = ESP.getMinFreeHeap();
    DEBUG_PRINTLN("🏥 System health monitoring initialized");

    // Initialize MQTT after WiFi
    setupMQTT();

    // Initialize modular components
    if (!initModbusProxy()) {
        DEBUG_PRINTLN("❌ Failed to initialize MODBUS proxy");
        ESP.restart();
    }

    if (!initEVCCAPI()) {
        DEBUG_PRINTLN("❌ Failed to initialize EVCC API");
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
    DEBUG_PRINTLN("   ✅ MQTT task created (Core 0, Priority 1)");

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
    DEBUG_PRINTLN("   ✅ Proxy task created (Core 1, Priority 2)");

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
    DEBUG_PRINTLN("   ✅ Watchdog task created (Core 0, Priority 3)");

    DEBUG_PRINTLN("🔗 Modular ESP32-S3 proxy initialized!");
    DEBUG_PRINTLN("   📡 MQTT publishing and EVCC API polling");
    DEBUG_PRINTLN("   🔄 MODBUS proxy with power correction");
    DEBUG_PRINTLN("   🐕 Independent health monitoring");
    DEBUG_PRINTLN("⚡ Ready for operations!");

    // Phase 4: Setup complete - Blink green 5 times
    for (int i = 0; i < 5; i++) {
        pixel.setPixelColor(0, pixel.Color(0, 255, 0));
        pixel.show();
        delay(100);
        pixel.setPixelColor(0, pixel.Color(0, 0, 0));
        pixel.show();
        delay(100);
    }
}

void loop() {
    ArduinoOTA.handle();
    vTaskDelay(1000);
}