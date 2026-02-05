#pragma once

#include <Arduino.h>
#include "nvs_config.h"

// Log entry structure
struct LogEntry {
  uint32_t timestamp;
  uint8_t level;
  char subsystem[16];
  char message[128];
};

// Circular buffer size
#define LOG_BUFFER_SIZE 16

// Log level names
extern const char* LOG_LEVEL_NAMES[];

// Function declarations
void initMQTTLogger();
void logMessage(uint8_t level, const char* subsystem, const char* format, ...);
bool isLogQueueEmpty();
uint8_t getLogQueueCount();
bool getNextLogEntry(LogEntry& entry);

// Convenience macros for logging
#define MLOG_DEBUG(subsys, fmt, ...) logMessage(LOG_LEVEL_DEBUG, subsys, fmt, ##__VA_ARGS__)
#define MLOG_INFO(subsys, fmt, ...)  logMessage(LOG_LEVEL_INFO, subsys, fmt, ##__VA_ARGS__)
#define MLOG_WARN(subsys, fmt, ...)  logMessage(LOG_LEVEL_WARN, subsys, fmt, ##__VA_ARGS__)
#define MLOG_ERROR(subsys, fmt, ...) logMessage(LOG_LEVEL_ERROR, subsys, fmt, ##__VA_ARGS__)

// Global flag for MQTT connectivity (set by mqtt_handler)
extern bool mqttLoggerConnected;
