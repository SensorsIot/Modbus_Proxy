#include "mqtt_handler.h"
#include "credentials.h"
#include "config.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t mqtt_client = NULL;
system_health_t system_health = {0};

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)event;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            break;
        default:
            break;
    }
}

bool mqtt_init(void)
{
    char uri[128];
    snprintf(uri, sizeof(uri), "mqtt://%s:%d", MQTT_SERVER, MQTT_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .buffer.size = 2048,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return false;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "MQTT client started");
    return true;
}

bool mqtt_is_connected(void)
{
    return mqtt_client != NULL;
}

void mqtt_publish_power_data(const dtsu666_data_t* dtsu, float correction, bool correction_active)
{
    if (!mqtt_client || !dtsu) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "dtsu", dtsu->power_total);
    cJSON_AddNumberToObject(root, "wallbox", correction);
    cJSON_AddNumberToObject(root, "sun2000", dtsu->power_total + (correction_active ? correction : 0.0f));
    cJSON_AddBoolToObject(root, "active", correction_active);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_POWER, json_str, 0, 0, 0);
        free(json_str);
    }

    cJSON_Delete(root);
}

void mqtt_publish_health(const system_health_t* health)
{
    if (!mqtt_client || !health) return;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime", health->uptime);
    cJSON_AddNumberToObject(root, "free_heap", health->freeHeap);
    cJSON_AddNumberToObject(root, "min_free_heap", health->minFreeHeap);
    cJSON_AddNumberToObject(root, "mqtt_reconnects", health->mqttReconnects);
    cJSON_AddNumberToObject(root, "dtsu_updates", health->dtsuUpdates);
    cJSON_AddNumberToObject(root, "evcc_updates", health->evccUpdates);
    cJSON_AddNumberToObject(root, "evcc_errors", health->evccErrors);
    cJSON_AddNumberToObject(root, "proxy_errors", health->proxyErrors);
    cJSON_AddNumberToObject(root, "power_correction", health->lastPowerCorrection);
    cJSON_AddBoolToObject(root, "correction_active", health->powerCorrectionActive);

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC_HEALTH, json_str, 0, 0, 0);
        free(json_str);
    }

    cJSON_Delete(root);
}
