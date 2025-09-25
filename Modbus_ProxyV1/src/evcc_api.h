#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// Thread-safe shared EVCC data structure
struct SharedEVCCData {
  SemaphoreHandle_t mutex;              // Mutex for thread-safe access
  float chargePower;                    // Charge power from EVCC API (W)
  uint32_t timestamp;                   // When data was last updated (millis)
  bool valid;                           // Whether data is valid
  uint32_t updateCount;                 // Number of successful API calls
  uint32_t errorCount;                  // Number of failed API calls
};

// Function declarations
bool initEVCCAPI();
bool pollEvccApi(SharedEVCCData& sharedData);
float calculatePowerCorrection(const SharedEVCCData& evccData);
bool isEVCCDataValid(const SharedEVCCData& evccData);
void getEVCCData(const SharedEVCCData& sharedData, float& chargePower, bool& valid);

// HTTP client utilities
bool httpGetJSON(const char* url, JsonDocument& doc);
void logAPIError(const String& error, int httpCode = 0);