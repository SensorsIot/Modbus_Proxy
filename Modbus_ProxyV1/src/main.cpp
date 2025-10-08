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
#include "debug.h"
#include "dtsu666.h"
#include "evcc_api.h"
#include "mqtt_handler.h"
#include "modbus_proxy.h"
#include <ArduinoOTA.h>

// Global variable definitions are in credentials.h

// Telnet debug instance
#if defined(ENABLE_TELNET_DEBUG) && ENABLE_TELNET_DEBUG
TelnetDebug telnetDebug;
#endif

// Output options
#ifndef PRINT_FANCY_TABLE
#define PRINT_FANCY_TABLE false
#endif

// WiFi and MQTT setup functions
void setupWiFi();
void setupMQTT();
void discoverModbusPins();

void setupWiFi() {
  // Disable WiFi power saving to prevent spinlock issues
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);

  DEBUG_PRINTF("📡 Connecting to WiFi SSID: %s\n", ssid);
  WiFi.setHostname("MODBUS-Proxy");
  int status = WiFi.begin(ssid, password);
  DEBUG_PRINTF("WiFi.begin() returned: %d\n", status);

  uint32_t startTime = millis();
  const uint32_t WIFI_TIMEOUT = 30000;  // 30 second timeout

  // Phase 2: WiFi connection - Blink during attempts
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime > WIFI_TIMEOUT) {
      DEBUG_PRINTLN();
      DEBUG_PRINTF("❌ WiFi connection timeout - Final status: %d\n", WiFi.status());
      DEBUG_PRINTLN("Restarting...");
      ESP.restart();
    }

    // Blink LED
    LED_ON();
    delay(350);
    LED_OFF();
    delay(350);

    DEBUG_PRINTF("[%d]", WiFi.status());
  }

  DEBUG_PRINTLN();
  DEBUG_PRINTF("WiFi connected! IP address: %s\n", WiFi.localIP().toString().c_str());
  DEBUG_PRINTLN("✅ WiFi power saving disabled for stability");

  // Blink 2 times to indicate WiFi connection success
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

    // Flash LED rapidly on error
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
    // Wait briefly for USB CDC to be ready on ESP32-C3
    delay(100);
#endif

    // Initialize status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    LED_OFF();

    // Phase 1: Startup - Blink 5 times
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

    // Initialize WiFi FIRST, before any other subsystems
    setupWiFi();
    setupOTA();

    // Initialize Telnet debug server after WiFi
#if defined(ENABLE_TELNET_DEBUG) && ENABLE_TELNET_DEBUG
    telnetDebug.begin(23);
    DEBUG_PRINTLN("📡 Telnet debug server started on port 23");
    DEBUG_PRINTF("   Connect via: telnet %s 23\n", WiFi.localIP().toString().c_str());
#endif

    // Initialize system health monitoring
    uint32_t currentTime = millis();
    systemHealth.uptime = currentTime;
    systemHealth.freeHeap = ESP.getFreeHeap();
    systemHealth.minFreeHeap = ESP.getMinFreeHeap();
    DEBUG_PRINTLN("🏥 System health monitoring initialized");

    // Initialize MQTT after WiFi
    setupMQTT();

    // Run pin discovery to find active MODBUS RX pins
    discoverModbusPins();

    // Initialize modular components
    if (!initModbusProxy()) {
        DEBUG_PRINTLN("❌ Failed to initialize MODBUS proxy");
        ESP.restart();
    }

    if (!initEVCCAPI()) {
        DEBUG_PRINTLN("❌ Failed to initialize EVCC API");
        ESP.restart();
    }

    // Create MQTT task (lowest priority) - increased stack for EVCC API + JSON
    xTaskCreate(
        mqttTask,
        "MQTTTask",
        16384, // Increased to 16KB for HTTP + large JSON buffer
        NULL,
        1,
        NULL
    );
    DEBUG_PRINTLN("   ✅ MQTT task created (Priority 1)");

    // Create proxy task (medium priority)
    xTaskCreate(
        proxyTask,
        "ProxyTask",
        4096,
        NULL,
        2,
        NULL
    );
    DEBUG_PRINTLN("   ✅ Proxy task created (Priority 2)");

    // Create watchdog task (highest priority)
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

    // Phase 4: Setup complete - Blink 5 times
    for (int i = 0; i < 5; i++) {
        LED_ON();
        delay(100);
        LED_OFF();
        delay(100);
    }
}

void discoverModbusPins() {
    DEBUG_PRINTLN("\n🔍 MODBUS PIN DISCOVERY MODE");
    DEBUG_PRINTLN("Scanning configured GPIO pins for MODBUS traffic...");

    // Test only the configured pins
    const uint8_t testPins[] = {RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN};
    const uint8_t numPins = sizeof(testPins) / sizeof(testPins[0]);

    struct PinActivity {
        uint8_t pin;
        uint32_t byteCount;
        uint32_t transitions;
    };

    PinActivity activity[numPins] = {};

    // Initialize all test pins as INPUT
    for (uint8_t i = 0; i < numPins; i++) {
        pinMode(testPins[i], INPUT);
        activity[i].pin = testPins[i];
        activity[i].byteCount = 0;
        activity[i].transitions = 0;
    }

    DEBUG_PRINTLN("⏱️  Monitoring for 15 seconds...");

    // Monitor for 15 seconds
    uint32_t startTime = millis();
    uint32_t lastReportTime = millis();
    uint8_t lastState[numPins];

    // Initialize last state
    for (uint8_t i = 0; i < numPins; i++) {
        lastState[i] = digitalRead(testPins[i]);
    }

    while (millis() - startTime < 15000) {
        for (uint8_t i = 0; i < numPins; i++) {
            uint8_t currentState = digitalRead(testPins[i]);
            if (currentState != lastState[i]) {
                activity[i].transitions++;
                lastState[i] = currentState;
            }
        }

        // Progress indicator every 3 seconds
        if (millis() - lastReportTime > 3000) {
            DEBUG_PRINTF("   ⏳ %lu seconds elapsed...\n", (millis() - startTime) / 1000);
            lastReportTime = millis();
        }

        // Feed watchdog and yield to prevent WDT reset
        yield();
        delayMicroseconds(500); // Sample at ~2kHz (sufficient for 9600 baud)
    }

    DEBUG_PRINTLN("\n📊 DISCOVERY RESULTS:");
    DEBUG_PRINTLN("GPIO | Transitions | Likely");
    DEBUG_PRINTLN("-----|-------------|-------");

    uint8_t candidateRxPins[4] = {255, 255, 255, 255};
    uint8_t candidateCount = 0;

    for (uint8_t i = 0; i < numPins; i++) {
        const char* likely = "";
        if (activity[i].transitions > 100) {
            likely = "← ACTIVE (RX candidate)";
            if (candidateCount < 4) {
                candidateRxPins[candidateCount++] = activity[i].pin;
            }
        } else if (activity[i].transitions > 10) {
            likely = "← Some activity";
        }

        DEBUG_PRINTF("  %2d | %11lu | %s\n",
                     activity[i].pin,
                     activity[i].transitions,
                     likely);
    }

    DEBUG_PRINTLN();

    if (candidateCount >= 2) {
        DEBUG_PRINTLN("✅ FOUND CANDIDATE RX PINS:");
        DEBUG_PRINTF("   Likely SUN2000 RX: GPIO %d (or GPIO %d)\n",
                     candidateRxPins[0], candidateRxPins[1]);
        if (candidateCount > 2) {
            DEBUG_PRINTF("   Likely DTSU RX: GPIO %d (or GPIO %d)\n",
                         candidateRxPins[2], candidateCount > 3 ? candidateRxPins[3] : candidateRxPins[1]);
        }
        DEBUG_PRINTLN("\n💡 Update config.h with these pins and reflash.");
        DEBUG_PRINTLN("   Continuing with current configuration...\n");
    } else if (candidateCount == 1) {
        DEBUG_PRINTF("⚠️  WARNING: Only found 1 active pin (GPIO %d)\n", candidateRxPins[0]);
        DEBUG_PRINTLN("   Expected 2 active RX pins (SUN2000 and DTSU)");
        DEBUG_PRINTLN("   Check your connections and try again.\n");
    } else {
        DEBUG_PRINTLN("❌ ERROR: No MODBUS traffic detected on any GPIO!");
        DEBUG_PRINTLN("   Possible issues:");
        DEBUG_PRINTLN("   - RS485 adapters not connected");
        DEBUG_PRINTLN("   - SUN2000 not polling DTSU");
        DEBUG_PRINTLN("   - Wrong baud rate or wiring");
        DEBUG_PRINTLN("   - RS485 A/B lines swapped");
        DEBUG_PRINTLN("\n⏸️  Halting - fix connections and restart.\n");

        // Blink LED rapidly to indicate error
        while (true) {
            LED_ON();
            delay(100);
            LED_OFF();
            delay(100);
        }
    }
}

void loop() {
    ArduinoOTA.handle();
    DEBUG_HANDLE();  // Handle telnet client connections
    vTaskDelay(100);  // Faster loop for responsive telnet
}