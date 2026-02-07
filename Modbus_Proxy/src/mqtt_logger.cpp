#include "mqtt_logger.h"
#include "config.h"
#include <stdarg.h>

// Log level names
const char* LOG_LEVEL_NAMES[] = {"DEBUG", "INFO", "WARN", "ERROR"};

// Circular buffer for log entries
static LogEntry logBuffer[LOG_BUFFER_SIZE];
static volatile uint8_t logHead = 0;
static volatile uint8_t logTail = 0;
static volatile uint8_t logCount = 0;

// Mutex for thread-safe access
static SemaphoreHandle_t logMutex = NULL;

// Flag indicating MQTT is connected (updated by mqtt_handler)
bool mqttLoggerConnected = false;

void initMQTTLogger() {
  logMutex = xSemaphoreCreateMutex();
  if (logMutex == NULL) {
    Serial.println("Failed to create log mutex");
  }
  logHead = 0;
  logTail = 0;
  logCount = 0;
  Serial.println("MQTT Logger initialized");
}

void logMessage(uint8_t level, const char* subsystem, const char* format, ...) {
  // Always output to Serial
  char buffer[128];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  // Serial output with timestamp and level
  Serial.printf("[%lu][%s][%s] %s\n", millis(), LOG_LEVEL_NAMES[level], subsystem, buffer);

  // Check debug mode - if enabled, always queue DEBUG level messages
  bool debugModeEnabled = isDebugModeEnabled();

  // Determine minimum level for MQTT queueing
  // If debug mode is enabled, queue all messages (including DEBUG)
  // Otherwise, use configured log level
  uint8_t minLevel = debugModeEnabled ? LOG_LEVEL_DEBUG : mqttConfig.logLevel;

  if (level < minLevel) {
    return;
  }

  // Queue for MQTT transmission
  if (logMutex == NULL) {
    return;
  }

  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // If buffer is full, overwrite oldest entry
    if (logCount >= LOG_BUFFER_SIZE) {
      logTail = (logTail + 1) % LOG_BUFFER_SIZE;
    } else {
      logCount++;
    }

    LogEntry& entry = logBuffer[logHead];
    entry.timestamp = millis();
    entry.level = level;
    strncpy(entry.subsystem, subsystem, sizeof(entry.subsystem) - 1);
    entry.subsystem[sizeof(entry.subsystem) - 1] = '\0';
    strncpy(entry.message, buffer, sizeof(entry.message) - 1);
    entry.message[sizeof(entry.message) - 1] = '\0';

    logHead = (logHead + 1) % LOG_BUFFER_SIZE;

    xSemaphoreGive(logMutex);
  }
}

// Called from mqtt_handler when connected - returns true if entry was retrieved
bool getNextLogEntry(LogEntry& entry) {
  if (logMutex == NULL || logCount == 0) {
    return false;
  }

  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (logCount == 0) {
      xSemaphoreGive(logMutex);
      return false;
    }

    entry = logBuffer[logTail];
    logTail = (logTail + 1) % LOG_BUFFER_SIZE;
    logCount--;

    xSemaphoreGive(logMutex);
    return true;
  }

  return false;
}

// processLogQueue() is implemented in mqtt_handler.cpp
// since it needs access to mqttClient

bool isLogQueueEmpty() {
  return logCount == 0;
}

uint8_t getLogQueueCount() {
  return logCount;
}
