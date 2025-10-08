#include "evcc_api.h"
#include "credentials.h"
#include "config.h"
#include "debug.h"

// HTTP client mutex for thread safety
SemaphoreHandle_t httpClientMutex = NULL;

bool initEVCCAPI() {
  httpClientMutex = xSemaphoreCreateMutex();
  if (httpClientMutex == NULL) {
    DEBUG_PRINTLN("❌ Failed to create HTTP client mutex");
    return false;
  }
  DEBUG_PRINTLN("✅ HTTP client mutex created");
  return true;
}

bool pollEvccApi(SharedEVCCData& sharedData) {
  // Protect HTTP client access with mutex
  if (xSemaphoreTake(httpClientMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    DEBUG_PRINTLN("❌ Failed to acquire HTTP mutex");
    return false;
  }

  HTTPClient http;
  http.begin(evccApiUrl);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  int httpResponseCode = http.GET();

  if (httpResponseCode != HTTP_CODE_OK) {
    http.end();
    xSemaphoreGive(httpClientMutex);  // Release HTTP mutex

    if (xSemaphoreTake(sharedData.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
      sharedData.errorCount++;
      xSemaphoreGive(sharedData.mutex);
    }

    logAPIError("HTTP request failed", httpResponseCode);
    return false;
  }

  String payload = http.getString();
  http.end();
  xSemaphoreGive(httpClientMutex);  // Release HTTP mutex after HTTP operations complete

  if (payload.isEmpty()) {
    if (xSemaphoreTake(sharedData.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
      sharedData.errorCount++;
      xSemaphoreGive(sharedData.mutex);
    }
    logAPIError("Empty response");
    return false;
  }


  StaticJsonDocument<8192> doc;   // Back to stack-based with larger task stack
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    if (xSemaphoreTake(sharedData.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
      sharedData.errorCount++;
      xSemaphoreGive(sharedData.mutex);
    }
    DEBUG_PRINTF("❌ JSON parse error: %s\n", error.c_str());
    logAPIError("JSON parsing failed: " + String(error.c_str()));
    return false;
  }


  if (!doc.containsKey("loadpoints") || !doc["loadpoints"].is<JsonArray>() ||
      doc["loadpoints"].size() == 0 || !doc["loadpoints"][0].containsKey("chargePower")) {
    if (xSemaphoreTake(sharedData.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
      sharedData.errorCount++;
      xSemaphoreGive(sharedData.mutex);
    }
    logAPIError("Missing loadpoints[0].chargePower field");
    return false;
  }

  float chargePower = doc["loadpoints"][0]["chargePower"].as<float>();

  if (xSemaphoreTake(sharedData.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
    sharedData.chargePower = chargePower;
    sharedData.timestamp = millis();
    sharedData.valid = true;
    sharedData.updateCount++;
    xSemaphoreGive(sharedData.mutex);
    return true;
  }

  return false;
}

float calculatePowerCorrection(const SharedEVCCData& evccData) {
  if (!isEVCCDataValid(evccData)) {
    return 0.0f;
  }

  float wallboxPower = 0.0f;
  bool valid = false;
  getEVCCData(evccData, wallboxPower, valid);

  if (!valid || fabs(wallboxPower) <= CORRECTION_THRESHOLD) {
    return 0.0f;
  }

  return wallboxPower;
}

bool isEVCCDataValid(const SharedEVCCData& evccData) {
  if (xSemaphoreTake(evccData.mutex, 5 / portTICK_PERIOD_MS) == pdTRUE) {
    bool valid = evccData.valid && (millis() - evccData.timestamp) <= EVCC_DATA_MAX_AGE_MS;
    xSemaphoreGive(evccData.mutex);
    return valid;
  }
  return false;
}

void getEVCCData(const SharedEVCCData& sharedData, float& chargePower, bool& valid) {
  chargePower = 0.0f;
  valid = false;

  if (xSemaphoreTake(sharedData.mutex, 5 / portTICK_PERIOD_MS) == pdTRUE) {
    if (sharedData.valid && (millis() - sharedData.timestamp) <= EVCC_DATA_MAX_AGE_MS) {
      chargePower = sharedData.chargePower;
      valid = true;
    }
    xSemaphoreGive(sharedData.mutex);
  }
}

bool httpGetJSON(const char* url, JsonDocument& doc) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000);

  int httpResponseCode = http.GET();
  if (httpResponseCode != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  if (payload.isEmpty()) {
    return false;
  }

  DeserializationError error = deserializeJson(doc, payload);
  return error == DeserializationError::Ok;
}

void logAPIError(const String& error, int httpCode) {
  if (httpCode != 0) {
    DEBUG_PRINTF("❌ EVCC API Error: %s (HTTP %d)\n", error.c_str(), httpCode);
  } else {
    DEBUG_PRINTF("❌ EVCC API Error: %s\n", error.c_str());
  }
}