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
#include "wallbox_data.h"
#include "mqtt_handler.h"
#include "modbus_proxy.h"
#include "nvs_config.h"
#include "mqtt_logger.h"
#include "wifi_manager.h"
#include "web_server.h"
#include <ESPmDNS.h>

// Global variable definitions are in credentials.h

// Output options
#ifndef PRINT_FANCY_TABLE
#define PRINT_FANCY_TABLE false
#endif

// MQTT setup function
void setupMQTT();

// Captive portal task
void captivePortalTask(void *pvParameters);

void setupMQTT() {
  initMQTT();
}

void captivePortalTask(void *pvParameters) {
  (void)pvParameters;
  DEBUG_PRINTLN("Captive portal task started");

  uint32_t startTime = millis();

  while (captivePortalActive) {
    // Handle DNS requests for captive portal
    handleCaptivePortalDNS();

    // Check timeout
    if ((millis() - startTime) > CAPTIVE_PORTAL_TIMEOUT_MS) {
      DEBUG_PRINTLN("Captive portal timeout, restarting...");
      delay(500);
      ESP.restart();
    }

    // Blink LED slowly to indicate portal mode
    static uint32_t lastBlink = 0;
    if (millis() - lastBlink > 1000) {
      LED_ON();
      delay(100);
      LED_OFF();
      lastBlink = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  DEBUG_PRINTLN("Captive portal task ending");
  vTaskDelete(NULL);
}

void setup() {
#if SERIAL_DEBUG_LEVEL > 0
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

    DEBUG_PRINTLN("ESP32-C3 MODBUS PROXY starting...");
    DEBUG_PRINTF("Build: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTLN("Mode: Modular ESP32-C3 single-core proxy with configurable GPIO pins");

    // Initialize NVS configuration FIRST
    DEBUG_PRINTLN("\nInitializing NVS configuration...");
    if (!initNVSConfig()) {
        DEBUG_PRINTLN("NVS init failed, using defaults");
    }

    // GPIO 2 button check for captive portal trigger
    pinMode(PORTAL_BUTTON_PIN, INPUT_PULLUP);
    delay(10);  // Allow pull-up to stabilize
    bool portalButtonPressed = (digitalRead(PORTAL_BUTTON_PIN) == LOW);
    DEBUG_PRINTF("Portal button (GPIO%d): %s\n", PORTAL_BUTTON_PIN, portalButtonPressed ? "PRESSED" : "released");

    if (portalButtonPressed) {
        DEBUG_PRINTLN("\n*** CAPTIVE PORTAL MODE TRIGGERED ***");
        DEBUG_PRINTLN("Portal button held during boot, entering WiFi setup mode...\n");

        // Initialize WiFi manager and enter AP mode
        initWiFiManager();
        if (enterCaptivePortalMode()) {
            // Start web server in portal mode
            initWebServer(WEB_MODE_PORTAL);

            // Create captive portal task
            xTaskCreate(
                captivePortalTask,
                "PortalTask",
                4096,
                NULL,
                1,
                NULL
            );

            // Stay in this loop until restart
            while (true) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        } else {
            DEBUG_PRINTLN("Failed to start captive portal, continuing normal boot...");
        }
    }

    // Initialize MQTT logger
    initMQTTLogger();

    DEBUG_PRINTLN("\nConfiguration Parameters:");
    DEBUG_PRINTF("   WiFi SSID: '%s'\n", ssid);
    DEBUG_PRINTF("   MQTT Server: %s:%d\n", mqttConfig.host, mqttConfig.port);
    DEBUG_PRINTF("   MQTT User: %s\n", mqttConfig.user);
    DEBUG_PRINTF("   Wallbox Topic: %s\n", mqttConfig.wallboxTopic);
    DEBUG_PRINTF("   Log Level: %d\n", mqttConfig.logLevel);
    DEBUG_PRINTF("   RS485 SUN2000: RX=%d, TX=%d\n", RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);
    DEBUG_PRINTF("   RS485 DTU: RX=%d, TX=%d\n", RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);
    DEBUG_PRINTF("   Status LED: GPIO %d\n", STATUS_LED_PIN);
    DEBUG_PRINTF("   MODBUS Baudrate: %d\n", MODBUS_BAUDRATE);
    DEBUG_PRINTF("   Power Correction Threshold: %.0f W\n", CORRECTION_THRESHOLD);
    DEBUG_PRINTF("   Wallbox Data Max Age: %d ms\n", WALLBOX_DATA_MAX_AGE_MS);
    DEBUG_PRINTF("   Watchdog Timeout: %d ms\n", WATCHDOG_TIMEOUT_MS);
    DEBUG_PRINTF("   Serial Debug Level: %d (%s)\n\n", SERIAL_DEBUG_LEVEL, SERIAL_DEBUG_LEVEL == 0 ? "OFF" : SERIAL_DEBUG_LEVEL == 1 ? "INFO" : "DEBUG");
    DEBUG_PRINTF("   Debug Mode: %s\n\n", isDebugModeEnabled() ? "ENABLED" : "DISABLED");

    // Initialize WiFi manager
    initWiFiManager();

    // Try to connect to WiFi
    WiFiState wifiState = connectWiFi(WIFI_CONNECT_TIMEOUT_MS);

    if (wifiState == WIFI_STATE_CONNECTED) {
        DEBUG_PRINTLN("WiFi connected");

        // Start mDNS responder
        if (MDNS.begin("modbus-proxy")) {
            MDNS.addService("http", "tcp", 80);
            DEBUG_PRINTLN("mDNS started: http://modbus-proxy.local");
        } else {
            DEBUG_PRINTLN("mDNS failed to start");
        }

        // Blink 2 times to indicate WiFi connection success
        for (int i = 0; i < 2; i++) {
            LED_ON();
            delay(200);
            LED_OFF();
            delay(200);
        }
    } else {
        // WiFi failed - restart after recovery timeout (never enters portal without button)
        DEBUG_PRINTF("WiFi failed, restarting in %d seconds...\n", WIFI_MQTT_RECOVERY_TIMEOUT_MS / 1000);
        delay(WIFI_MQTT_RECOVERY_TIMEOUT_MS);
        ESP.restart();
    }

    // Initialize system health monitoring
    uint32_t currentTime = millis();
    systemHealth.uptime = currentTime;
    systemHealth.freeHeap = ESP.getFreeHeap();
    systemHealth.minFreeHeap = ESP.getMinFreeHeap();
    DEBUG_PRINTLN("System health monitoring initialized");

    // Initialize MQTT after WiFi
    setupMQTT();

    // Start web server in normal mode
    if (!initWebServer(WEB_MODE_NORMAL)) {
        DEBUG_PRINTLN("Warning: Web server failed to start");
    }

    // Initialize modular components
    if (!initModbusProxy()) {
        DEBUG_PRINTLN("Failed to initialize MODBUS proxy");
        ESP.restart();
    }

    if (!initWallboxData()) {
        DEBUG_PRINTLN("Failed to initialize wallbox data");
        ESP.restart();
    }

    // Create MQTT task (lowest priority) - handles MQTT and wallbox subscriptions
    xTaskCreate(
        mqttTask,
        "MQTTTask",
        8192,
        NULL,
        1,
        NULL
    );
    DEBUG_PRINTLN("   MQTT task created (Priority 1)");

    // Create proxy task (medium priority)
    xTaskCreate(
        proxyTask,
        "ProxyTask",
        4096,
        NULL,
        2,
        NULL
    );
    DEBUG_PRINTLN("   Proxy task created (Priority 2)");

    // Create watchdog task (highest priority)
    xTaskCreate(
        watchdogTask,
        "WatchdogTask",
        2048,
        NULL,
        3,
        NULL
    );
    DEBUG_PRINTLN("   Watchdog task created (Priority 3)");

    DEBUG_PRINTLN("Modular ESP32-C3 proxy initialized!");
    DEBUG_PRINTLN("   MQTT publishing and wallbox subscription");
    DEBUG_PRINTLN("   MODBUS proxy with power correction");
    DEBUG_PRINTLN("   Independent health monitoring");
    DEBUG_PRINTLN("Ready for operations!");

    // Phase 4: Setup complete - Blink 5 times
    for (int i = 0; i < 5; i++) {
        LED_ON();
        delay(100);
        LED_OFF();
        delay(100);
    }
}

void loop() {
    vTaskDelay(100);
}
