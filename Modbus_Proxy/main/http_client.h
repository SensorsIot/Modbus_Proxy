#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

typedef struct {
    SemaphoreHandle_t mutex;
    float chargePower;
    uint32_t timestamp;
    bool valid;
    uint32_t updateCount;
    uint32_t errorCount;
} shared_evcc_data_t;

extern shared_evcc_data_t shared_evcc;

bool http_client_init(void);
bool http_poll_evcc_api(shared_evcc_data_t* data);
bool http_get_evcc_data(const shared_evcc_data_t* data, float* charge_power, bool* valid);
float http_calculate_power_correction(const shared_evcc_data_t* evcc_data);
