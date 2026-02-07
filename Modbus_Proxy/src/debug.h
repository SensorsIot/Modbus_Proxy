#pragma once

#include <Arduino.h>
#include "config.h"

#if SERIAL_DEBUG_LEVEL >= 2
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif

// Include MQTT logger for MLOG macros
#include "mqtt_logger.h"
