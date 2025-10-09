#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "config.h"
#include "wifi_handler.h"
#include "mqtt_handler.h"
#include "http_client.h"
#include "modbus_proxy.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-C3 MODBUS PROXY starting...");
    ESP_LOGI(TAG, "Build: %s %s", __DATE__, __TIME__);
    ESP_LOGI(TAG, "Mode: ESP-IDF MODBUS proxy with power correction");

    ESP_LOGI(TAG, "\nConfiguration:");
    ESP_LOGI(TAG, "  RS485 SUN2000: RX=%d, TX=%d", RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);
    ESP_LOGI(TAG, "  RS485 DTU: RX=%d, TX=%d", RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);
    ESP_LOGI(TAG, "  Status LED: GPIO %d", STATUS_LED_PIN);
    ESP_LOGI(TAG, "  MODBUS Baudrate: %d", MODBUS_BAUDRATE);
    ESP_LOGI(TAG, "  Power Correction Threshold: %.0f W", CORRECTION_THRESHOLD);
    ESP_LOGI(TAG, "  HTTP Poll Interval: %d ms", HTTP_POLL_INTERVAL);
    ESP_LOGI(TAG, "  Watchdog Timeout: %d ms\n", WATCHDOG_TIMEOUT_MS);

    if (!wifi_init_sta()) {
        ESP_LOGE(TAG, "WiFi initialization failed");
        esp_restart();
    }

    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    wifi_wait_connected();
    ESP_LOGI(TAG, "WiFi connected!");

    if (!mqtt_init()) {
        ESP_LOGE(TAG, "MQTT initialization failed");
        esp_restart();
    }

    if (!http_client_init()) {
        ESP_LOGE(TAG, "HTTP client initialization failed");
        esp_restart();
    }

    if (!modbus_proxy_init()) {
        ESP_LOGI(TAG, "MODBUS proxy initialization failed");
        esp_restart();
    }

    xTaskCreate(mqtt_task, "MQTTTask", 16384, NULL, 1, NULL);
    ESP_LOGI(TAG, "MQTT task created (Priority 1)");

    xTaskCreate(modbus_proxy_task, "ProxyTask", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "Proxy task created (Priority 2)");

    xTaskCreate(watchdog_task, "WatchdogTask", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "Watchdog task created (Priority 3)");

    ESP_LOGI(TAG, "ESP32-C3 proxy initialized!");
    ESP_LOGI(TAG, "  MQTT publishing and EVCC API polling");
    ESP_LOGI(TAG, "  MODBUS proxy with power correction");
    ESP_LOGI(TAG, "  Independent health monitoring");
    ESP_LOGI(TAG, "Ready for operations!");
}
