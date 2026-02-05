#pragma once

#include <Arduino.h>
#include "config.h"

// Thread-safe shared Wallbox data structure
struct SharedWallboxData {
  SemaphoreHandle_t mutex;              // Mutex for thread-safe access
  float chargePower;                    // Charge power from wallbox (W)
  uint32_t timestamp;                   // When data was last updated (millis)
  bool valid;                           // Whether data is valid
  bool wasValid;                        // Previous validity state (for transition detection)
  uint32_t updateCount;                 // Number of successful updates
  uint32_t errorCount;                  // Number of failed updates
  uint32_t staleCount;                  // Number of times data went stale
};

// Function declarations
bool initWallboxData();
void updateWallboxPower(float power);
float getWallboxPower();
bool isWallboxDataValid();
void getWallboxData(float& chargePower, bool& valid);
float calculatePowerCorrection();

// Global shared data instance
extern SharedWallboxData sharedWallbox;
