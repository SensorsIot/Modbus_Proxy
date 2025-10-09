#pragma once

#include <stdbool.h>
#include "modbus_rtu.h"
#include "dtsu666.h"

bool modbus_proxy_init(void);
void modbus_proxy_task(void *pvParameters);
void watchdog_task(void *pvParameters);
void mqtt_task(void *pvParameters);
