#include "wallbox_data.h"
#include "debug.h"
#include "mqtt_logger.h"

// Global shared wallbox data
SharedWallboxData sharedWallbox = {NULL, 0.0f, 0, false, false, 0, 0, 0};

bool initWallboxData() {
  sharedWallbox.mutex = xSemaphoreCreateMutex();
  if (sharedWallbox.mutex == NULL) {
    DEBUG_PRINTLN("Failed to create wallbox data mutex");
    return false;
  }

  sharedWallbox.chargePower = 0.0f;
  sharedWallbox.timestamp = 0;
  sharedWallbox.valid = false;
  sharedWallbox.wasValid = false;
  sharedWallbox.updateCount = 0;
  sharedWallbox.errorCount = 0;
  sharedWallbox.staleCount = 0;

  DEBUG_PRINTLN("Wallbox data initialized");
  MLOG_INFO("WALLBOX", "Wallbox data subsystem initialized");
  return true;
}

void updateWallboxPower(float power) {
  if (xSemaphoreTake(sharedWallbox.mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    // Track transition from invalid/stale to valid
    bool wasStale = !sharedWallbox.valid ||
                    (sharedWallbox.timestamp > 0 && (millis() - sharedWallbox.timestamp) > WALLBOX_DATA_MAX_AGE_MS);

    sharedWallbox.chargePower = power;
    sharedWallbox.timestamp = millis();
    sharedWallbox.valid = true;
    sharedWallbox.wasValid = true;
    sharedWallbox.updateCount++;

    // Log transition from stale to valid
    if (wasStale && sharedWallbox.updateCount > 1) {
      MLOG_INFO("WALLBOX", "Data restored: %.1fW", power);
    }

    xSemaphoreGive(sharedWallbox.mutex);
  }
}

float getWallboxPower() {
  float power = 0.0f;

  if (xSemaphoreTake(sharedWallbox.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    if (sharedWallbox.valid && (millis() - sharedWallbox.timestamp) <= WALLBOX_DATA_MAX_AGE_MS) {
      power = sharedWallbox.chargePower;
    }
    xSemaphoreGive(sharedWallbox.mutex);
  }

  return power;
}

bool isWallboxDataValid() {
  bool valid = false;

  if (xSemaphoreTake(sharedWallbox.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    bool currentlyValid = sharedWallbox.valid && (millis() - sharedWallbox.timestamp) <= WALLBOX_DATA_MAX_AGE_MS;

    // Detect transition from valid to stale
    if (sharedWallbox.wasValid && !currentlyValid && sharedWallbox.timestamp > 0) {
      sharedWallbox.wasValid = false;
      sharedWallbox.staleCount++;
      uint32_t age = millis() - sharedWallbox.timestamp;
      MLOG_WARN("WALLBOX", "Data stale (age %lums > %lums), correction disabled", age, WALLBOX_DATA_MAX_AGE_MS);
    }

    valid = currentlyValid;
    xSemaphoreGive(sharedWallbox.mutex);
  }

  return valid;
}

void getWallboxData(float& chargePower, bool& valid) {
  chargePower = 0.0f;
  valid = false;

  if (xSemaphoreTake(sharedWallbox.mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    bool currentlyValid = sharedWallbox.valid && (millis() - sharedWallbox.timestamp) <= WALLBOX_DATA_MAX_AGE_MS;

    // Detect transition from valid to stale
    if (sharedWallbox.wasValid && !currentlyValid && sharedWallbox.timestamp > 0) {
      sharedWallbox.wasValid = false;
      sharedWallbox.staleCount++;
      uint32_t age = millis() - sharedWallbox.timestamp;
      MLOG_WARN("WALLBOX", "Data stale (age %lums > %lums), correction disabled", age, WALLBOX_DATA_MAX_AGE_MS);
    }

    if (currentlyValid) {
      chargePower = sharedWallbox.chargePower;
      valid = true;
    }
    xSemaphoreGive(sharedWallbox.mutex);
  }
}

float calculatePowerCorrection() {
  if (!isWallboxDataValid()) {
    return 0.0f;
  }

  float wallboxPower = getWallboxPower();

  if (fabs(wallboxPower) <= CORRECTION_THRESHOLD) {
    return 0.0f;
  }

  return wallboxPower;
}
