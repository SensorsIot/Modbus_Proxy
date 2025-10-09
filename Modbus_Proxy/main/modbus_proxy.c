#include "modbus_proxy.h"
#include "config.h"
#include "http_client.h"
#include "mqtt_handler.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <math.h>

static const char *TAG = "PROXY";

static modbus_rtu_t modbus_sun;
static modbus_rtu_t modbus_dtu;

static shared_dtsu_data_t shared_dtsu = {0};
static float power_correction = 0.0f;
static bool power_correction_active = false;

bool modbus_proxy_init(void)
{
    shared_dtsu.mutex = xSemaphoreCreateMutex();
    if (!shared_dtsu.mutex) {
        ESP_LOGE(TAG, "Failed to create DTSU mutex");
        return false;
    }

    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << STATUS_LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
    gpio_set_level(STATUS_LED_PIN, LED_INVERTED ? 1 : 0);

    modbus_rtu_init(&modbus_sun, UART_NUM_0, RS485_SUN2000_TX_PIN, RS485_SUN2000_RX_PIN, MODBUS_BAUDRATE);
    modbus_rtu_init(&modbus_dtu, UART_NUM_1, RS485_DTU_TX_PIN, RS485_DTU_RX_PIN, MODBUS_BAUDRATE);

    ESP_LOGI(TAG, "MODBUS proxy initialized");
    return true;
}

void modbus_proxy_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MODBUS Proxy task started");

    modbus_message_t sun_msg;
    uint32_t proxy_count = 0;

    while (1) {
        if (modbus_read(&modbus_sun, &sun_msg, 2000)) {
            gpio_set_level(STATUS_LED_PIN, LED_INVERTED ? 0 : 1);

            proxy_count++;

            if (sun_msg.id == 11 && sun_msg.type == MB_TYPE_REQUEST) {
                int tx_len = uart_write_bytes(modbus_dtu.uart_num, sun_msg.raw, sun_msg.len);
                uart_wait_tx_done(modbus_dtu.uart_num, pdMS_TO_TICKS(100));

                if (tx_len == sun_msg.len) {
                    modbus_message_t dtu_msg;
                    if (modbus_read(&modbus_dtu, &dtu_msg, 1000)) {
                        if (dtu_msg.type == MB_TYPE_EXCEPTION) {
                            ESP_LOGW(TAG, "DTSU exception: 0x%02X", dtu_msg.exCode);
                            system_health.proxyErrors++;
                        } else if (dtu_msg.fc == 0x03 && dtu_msg.len >= 165) {
                            dtsu666_data_t dtsu_data;
                            if (dtsu666_parse_response(dtu_msg.raw, dtu_msg.len, &dtsu_data)) {
                                power_correction = http_calculate_power_correction(&shared_evcc);
                                power_correction_active = (fabsf(power_correction) >= CORRECTION_THRESHOLD);

                                dtsu666_data_t final_data = dtsu_data;
                                bool correction_applied = false;

                                if (power_correction_active) {
                                    uint8_t corrected_response[165];
                                    memcpy(corrected_response, dtu_msg.raw, dtu_msg.len);

                                    if (dtsu666_apply_power_correction(corrected_response, dtu_msg.len, power_correction)) {
                                        dtu_msg.raw = corrected_response;
                                        if (dtsu666_parse_response(corrected_response, dtu_msg.len, &final_data)) {
                                            correction_applied = true;
                                        }
                                    }
                                }

                                float wallbox_power = 0.0f;
                                bool valid = false;
                                http_get_evcc_data(&shared_evcc, &wallbox_power, &valid);

                                ESP_LOGI(TAG, "DTSU: %.1fW | Wallbox: %.1fW | SUN2000: %.1fW",
                                        dtsu_data.power_total, wallbox_power,
                                        final_data.power_total);

                                mqtt_publish_power_data(&final_data, power_correction, correction_applied);

                                if (xSemaphoreTake(shared_dtsu.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                                    shared_dtsu.valid = true;
                                    shared_dtsu.timestamp = esp_timer_get_time() / 1000;
                                    shared_dtsu.response_length = dtu_msg.len;
                                    memcpy(shared_dtsu.response_buffer, dtu_msg.raw, dtu_msg.len);
                                    shared_dtsu.parsed_data = final_data;
                                    shared_dtsu.update_count++;
                                    xSemaphoreGive(shared_dtsu.mutex);
                                }

                                system_health.dtsuUpdates++;
                            }
                        }

                        uart_write_bytes(modbus_sun.uart_num, dtu_msg.raw, dtu_msg.len);
                        uart_wait_tx_done(modbus_sun.uart_num, pdMS_TO_TICKS(100));
                    } else {
                        ESP_LOGW(TAG, "DTSU timeout");
                        system_health.proxyErrors++;
                    }
                } else {
                    ESP_LOGE(TAG, "Failed to write to DTSU");
                    system_health.proxyErrors++;
                }
            }

            gpio_set_level(STATUS_LED_PIN, LED_INVERTED ? 1 : 0);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void watchdog_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Watchdog task started");

    while (1) {
        system_health.uptime = esp_timer_get_time() / 1000;
        system_health.freeHeap = esp_get_free_heap_size();
        system_health.minFreeHeap = esp_get_minimum_free_heap_size();

        if (system_health.freeHeap < MIN_FREE_HEAP) {
            ESP_LOGW(TAG, "Low memory: %lu bytes", system_health.freeHeap);
        }

        vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL));
    }
}

void mqtt_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT task started");

    uint32_t last_evcc_poll = 0;
    uint32_t last_health_report = 0;

    while (1) {
        uint32_t now = esp_timer_get_time() / 1000;

        if (now - last_evcc_poll > HTTP_POLL_INTERVAL) {
            ESP_LOGI(TAG, "Polling EVCC API...");
            bool success = http_poll_evcc_api(&shared_evcc);
            if (success) {
                system_health.evccUpdates++;
            } else {
                system_health.evccErrors++;
            }
            last_evcc_poll = now;
        }

        if (now - last_health_report > 60000) {
            mqtt_publish_health(&system_health);
            last_health_report = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
