#ifndef PRODUCTION_BUILD
#include "test_inject.h"
#include "nvs_config.h"
#include "modbus_proxy.h"
#include "mqtt_handler.h"
#include "debug.h"
#include "config.h"
#include <ArduinoJson.h>
#include <math.h>

void handleApiTestInject(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  // Only available when debug mode is enabled
  if (!isDebugModeEnabled()) {
    request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Debug mode required\"}");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }

  // Read test values (defaults simulate typical meter readings)
  float power_total = doc["power_total"] | 5000.0f;
  float voltage = doc["voltage"] | 230.0f;
  float frequency = doc["frequency"] | 50.0f;
  float current = doc["current"] | 10.0f;

  // Build DTSU666Data with test values
  DTSU666Data testData = {};
  testData.current_L1 = current;
  testData.current_L2 = current;
  testData.current_L3 = current;
  testData.voltage_LN_avg = voltage;
  testData.voltage_L1N = voltage;
  testData.voltage_L2N = voltage;
  testData.voltage_L3N = voltage;
  testData.voltage_LL_avg = voltage * 1.732f;
  testData.voltage_L1L2 = voltage * 1.732f;
  testData.voltage_L2L3 = voltage * 1.732f;
  testData.voltage_L3L1 = voltage * 1.732f;
  testData.frequency = frequency;
  testData.power_total = power_total;
  testData.power_L1 = power_total / 3.0f;
  testData.power_L2 = power_total / 3.0f;
  testData.power_L3 = power_total / 3.0f;
  testData.demand_total = power_total;
  testData.demand_L1 = power_total / 3.0f;
  testData.demand_L2 = power_total / 3.0f;
  testData.demand_L3 = power_total / 3.0f;
  testData.pf_total = 0.99f;
  testData.pf_L1 = 0.99f;
  testData.pf_L2 = 0.99f;
  testData.pf_L3 = 0.99f;

  // Encode to wire format (applies power_scale = -1 to power/demand fields)
  uint8_t responseBuffer[165];
  if (!encodeDTSU666Response(testData, responseBuffer, 165)) {
    request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Encode failed\"}");
    return;
  }

  // Parse back from wire format (as the real proxy does)
  DTSU666Data originalData = {};
  parseDTSU666Response(responseBuffer, 165, originalData);

  // Calculate power correction from current wallbox data
  calculateProxyPowerCorrection();

  DTSU666Data finalData = originalData;
  bool correctionApplied = false;

  if (powerCorrectionActive && fabs(powerCorrection) >= CORRECTION_THRESHOLD) {
    uint8_t correctedResponse[165];
    memcpy(correctedResponse, responseBuffer, 165);

    if (applyPowerCorrection(correctedResponse, 165, powerCorrection)) {
      memcpy(responseBuffer, correctedResponse, 165);
      parseDTSU666Response(responseBuffer, 165, finalData);
      correctionApplied = true;
    }
  }

  // Update shared data (same as proxy task)
  updateSharedData(responseBuffer, 165, finalData);
  systemHealth.dtsuUpdates++;

  // Build response
  StaticJsonDocument<256> response;
  response["status"] = "ok";
  response["dtsu_power"] = originalData.power_total;
  response["wallbox_power"] = powerCorrection;
  response["correction_active"] = correctionApplied;
  response["sun2000_power"] = finalData.power_total;

  String responseStr;
  serializeJson(response, responseStr);
  request->send(200, "application/json", responseStr);
}
#endif // PRODUCTION_BUILD
