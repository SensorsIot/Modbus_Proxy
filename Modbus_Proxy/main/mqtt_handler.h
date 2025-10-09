#pragma once

#include <stdbool.h>
#include "dtsu666.h"

typedef struct {
    uint32_t uptime;
    uint32_t freeHeap;
    uint32_t minFreeHeap;
    uint32_t mqttReconnects;
    uint32_t dtsuUpdates;
    uint32_t evccUpdates;
    uint32_t evccErrors;
    uint32_t proxyErrors;
    float lastPowerCorrection;
    bool powerCorrectionActive;
    uint32_t lastHealthReport;
} system_health_t;

extern system_health_t system_health;

bool mqtt_init(void);
bool mqtt_is_connected(void);
void mqtt_publish_power_data(const dtsu666_data_t* dtsu, float correction, bool correction_active);
void mqtt_publish_health(const system_health_t* health);
