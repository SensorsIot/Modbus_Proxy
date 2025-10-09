#include "http_client.h"
#include "credentials.h"
#include "config.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <math.h>

static const char *TAG = "HTTP";
shared_evcc_data_t shared_evcc = {0};

#define MAX_HTTP_OUTPUT_BUFFER 8192
static char http_buffer[MAX_HTTP_OUTPUT_BUFFER];
static int http_buffer_len = 0;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (http_buffer_len + evt->data_len < MAX_HTTP_OUTPUT_BUFFER) {
                memcpy(http_buffer + http_buffer_len, evt->data, evt->data_len);
                http_buffer_len += evt->data_len;
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

bool http_client_init(void)
{
    shared_evcc.mutex = xSemaphoreCreateMutex();
    if (shared_evcc.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create EVCC mutex");
        return false;
    }

    ESP_LOGI(TAG, "HTTP client initialized");
    return true;
}

bool http_poll_evcc_api(shared_evcc_data_t* data)
{
    if (!data) return false;

    http_buffer_len = 0;
    memset(http_buffer, 0, sizeof(http_buffer));

    esp_http_client_config_t config = {
        .url = EVCC_API_URL,
        .event_handler = http_event_handler,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            data->errorCount++;
            xSemaphoreGive(data->mutex);
        }
        return false;
    }

    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (status_code != 200) {
        ESP_LOGE(TAG, "HTTP error: %d", status_code);
        return false;
    }

    http_buffer[http_buffer_len] = '\0';

    cJSON *root = cJSON_Parse(http_buffer);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            data->errorCount++;
            xSemaphoreGive(data->mutex);
        }
        return false;
    }

    cJSON *loadpoints = cJSON_GetObjectItem(root, "loadpoints");
    if (!cJSON_IsArray(loadpoints) || cJSON_GetArraySize(loadpoints) == 0) {
        ESP_LOGE(TAG, "No loadpoints in response");
        cJSON_Delete(root);
        return false;
    }

    cJSON *loadpoint0 = cJSON_GetArrayItem(loadpoints, 0);
    cJSON *chargePower = cJSON_GetObjectItem(loadpoint0, "chargePower");

    if (!cJSON_IsNumber(chargePower)) {
        ESP_LOGE(TAG, "chargePower not found");
        cJSON_Delete(root);
        return false;
    }

    float power = (float)cJSON_GetNumberValue(chargePower);

    if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        data->chargePower = power;
        data->timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
        data->valid = true;
        data->updateCount++;
        xSemaphoreGive(data->mutex);
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "EVCC API: chargePower = %.1fW", power);
    return true;
}

bool http_get_evcc_data(const shared_evcc_data_t* data, float* charge_power, bool* valid)
{
    if (!data || !charge_power || !valid) return false;

    *charge_power = 0.0f;
    *valid = false;

    if (xSemaphoreTake(data->mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (data->valid && (now - data->timestamp) <= EVCC_DATA_MAX_AGE_MS) {
            *charge_power = data->chargePower;
            *valid = true;
        }
        xSemaphoreGive(data->mutex);
    }

    return true;
}

float http_calculate_power_correction(const shared_evcc_data_t* evcc_data)
{
    float wallbox_power = 0.0f;
    bool valid = false;

    http_get_evcc_data(evcc_data, &wallbox_power, &valid);

    if (!valid || fabsf(wallbox_power) <= CORRECTION_THRESHOLD) {
        return 0.0f;
    }

    return wallbox_power;
}
