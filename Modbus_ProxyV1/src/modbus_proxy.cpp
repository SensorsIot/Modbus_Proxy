#include "modbus_proxy.h"
#include "debug.h"
#include <Adafruit_NeoPixel.h>

// External NeoPixel reference
extern Adafruit_NeoPixel pixel;

// Serial interfaces
HardwareSerial SerialSUN(2);
HardwareSerial SerialDTU(1);

// ModbusRTU485 instances
ModbusRTU485 modbusSUN;
ModbusRTU485 modbusDTU;

// Shared data structures
SharedDTSUData sharedDTSU = {NULL, false, 0, {}, 0, {}, 0};
SharedEVCCData sharedEVCC = {NULL, 0.0f, 0, false, 0, 0};
DTSU666Data dtsu666Data;
LastRequestInfo g_lastReq;

// Power correction variables
float powerCorrection = 0.0f;
bool powerCorrectionActive = false;
uint32_t lastCorrectionTime = 0;
uint32_t lastHttpPoll = 0;

// Task synchronization
SemaphoreHandle_t proxyTaskHealthMutex;
SemaphoreHandle_t mqttTaskHealthMutex;
uint32_t proxyTaskLastSeen = 0;
uint32_t mqttTaskLastSeen = 0;

bool initModbusProxy() {
  proxyTaskHealthMutex = xSemaphoreCreateMutex();
  mqttTaskHealthMutex = xSemaphoreCreateMutex();

  if (!proxyTaskHealthMutex || !mqttTaskHealthMutex) {
    DEBUG_PRINTLN("❌ Failed to create health monitoring mutexes");
    return false;
  }

  sharedDTSU.mutex = xSemaphoreCreateMutex();
  if (!sharedDTSU.mutex) {
    DEBUG_PRINTLN("❌ Failed to create DTSU data mutex");
    return false;
  }

  sharedEVCC.mutex = xSemaphoreCreateMutex();
  if (!sharedEVCC.mutex) {
    DEBUG_PRINTLN("❌ Failed to create EVCC data mutex");
    return false;
  }

  return initSerialInterfaces();
}

bool initSerialInterfaces() {
  DEBUG_PRINTLN("🔧 Initializing RS485 interfaces...");

  SerialSUN.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);
  DEBUG_PRINTF("   ✅ SUN2000 interface: UART2, %d baud, pins %d(RX)/%d(TX)\n",
                MODBUS_BAUDRATE, RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);

  SerialDTU.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);
  DEBUG_PRINTF("   ✅ DTSU-666 interface: UART1, %d baud, pins %d(RX)/%d(TX)\n",
                MODBUS_BAUDRATE, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);

  modbusSUN.begin(SerialSUN, MODBUS_BAUDRATE);
  DEBUG_PRINTLN("   ✅ SUN2000 MODBUS handler initialized");

  modbusDTU.begin(SerialDTU, MODBUS_BAUDRATE);
  DEBUG_PRINTLN("   ✅ DTSU-666 MODBUS handler initialized");

  return true;
}


void proxyTask(void *pvParameters) {
  DEBUG_PRINTLN("🐌 Simple Proxy Task started - Direct SUN2000 ↔ DTSU proxying");

  ModbusMessage sunMsg;
  uint32_t proxyCount = 0;

  while (true) {
    updateTaskHealthbeat(true);

    if (modbusSUN.read(sunMsg, 2000)) {
      // Flash white to indicate SUN2000 interface activity
      pixel.setPixelColor(0, pixel.Color(50, 50, 50));
      pixel.show();

      proxyCount++;
      uint32_t proxyStart = millis();

      if (sunMsg.id == 11 && sunMsg.type == MBType::Request) {
        uint32_t dtsuStart = millis();
        size_t written = SerialDTU.write(sunMsg.raw, sunMsg.len);
        SerialDTU.flush();

        if (written == sunMsg.len) {
          ModbusMessage dtsuMsg;
          if (modbusDTU.read(dtsuMsg, 1000)) {
            uint32_t dtsuTime = millis() - dtsuStart;

            if (dtsuMsg.type == MBType::Exception) {
              DEBUG_PRINTF("   ❌ DTSU EXCEPTION: Code=0x%02X\n", dtsuMsg.exCode);
              systemHealth.proxyErrors++;
              reportSystemError("MODBUS", "DTSU exception", dtsuMsg.exCode);
            } else if (dtsuMsg.fc == 0x03 && dtsuMsg.len >= 165) {
              DTSU666Data dtsuData;
              if (parseDTSU666Response(dtsuMsg.raw, dtsuMsg.len, dtsuData)) {
                dtsu666Data = dtsuData;

                // Debug: DTSU received values
                DEBUG_PRINTF("📥 DTSU->Proxy: P_tot=%.1fW P_L1=%.1fW P_L2=%.1fW P_L3=%.1fW I_L1=%.2fA I_L2=%.2fA I_L3=%.2fA\n",
                              dtsuData.power_total, dtsuData.power_L1, dtsuData.power_L2, dtsuData.power_L3,
                              dtsuData.current_L1, dtsuData.current_L2, dtsuData.current_L3);
                calculatePowerCorrection();

                DTSU666Data finalData = dtsuData;
                bool correctionAppliedSuccessfully = false;

                if (powerCorrectionActive && fabs(powerCorrection) >= CORRECTION_THRESHOLD) {
                  uint8_t correctedResponse[165];
                  memcpy(correctedResponse, dtsuMsg.raw, dtsuMsg.len);

                  if (applyPowerCorrection(correctedResponse, dtsuMsg.len, powerCorrection)) {
                    static uint8_t staticCorrectedResponse[165];
                    memcpy(staticCorrectedResponse, correctedResponse, dtsuMsg.len);
                    dtsuMsg.raw = staticCorrectedResponse;

                    if (parseDTSU666Response(staticCorrectedResponse, dtsuMsg.len, finalData)) {
                      correctionAppliedSuccessfully = true;
                    } else {
                      finalData.power_total += powerCorrection;
                      finalData.power_L1 += powerCorrection / 3.0f;
                      finalData.power_L2 += powerCorrection / 3.0f;
                      finalData.power_L3 += powerCorrection / 3.0f;
                      correctionAppliedSuccessfully = true;
                    }
                  }
                }

                queueCorrectedPowerData(finalData, dtsuData, correctionAppliedSuccessfully,
                                      correctionAppliedSuccessfully ? powerCorrection : 0.0f);

                float currentWallboxPower = 0.0f;
                bool valid = false;
                getEVCCData(sharedEVCC, currentWallboxPower, valid);

                float sun2000Value = dtsuData.power_total;
                if (correctionAppliedSuccessfully && powerCorrection > 0) {
                  sun2000Value = finalData.power_total;
                }

                DEBUG_PRINTF("DTSU: %.1fW\n", dtsuData.power_total);
                DEBUG_PRINTF("API:  %.1fW (%s)\n", currentWallboxPower, valid ? "valid" : "stale");
                DEBUG_PRINTF("SUN2000: %.1fW (DTSU %.1fW + correction %.1fW)\n",
                            sun2000Value, dtsuData.power_total,
                            correctionAppliedSuccessfully ? powerCorrection : 0.0f);

                updateSharedData(dtsuMsg.raw, dtsuMsg.len, finalData);
                systemHealth.dtsuUpdates++;
              }
            }

            uint32_t sunReplyStart = millis();

            // Debug: Parse final data being sent to SUN2000
            DTSU666Data finalSentData;
            if (parseDTSU666Response(dtsuMsg.raw, dtsuMsg.len, finalSentData)) {
              DEBUG_PRINTF("📤 Proxy->SUN2000: P_tot=%.1fW P_L1=%.1fW P_L2=%.1fW P_L3=%.1fW I_L1=%.2fA I_L2=%.2fA I_L3=%.2fA\n",
                            finalSentData.power_total, finalSentData.power_L1, finalSentData.power_L2, finalSentData.power_L3,
                            finalSentData.current_L1, finalSentData.current_L2, finalSentData.current_L3);
            }

            size_t sunWritten = SerialSUN.write(dtsuMsg.raw, dtsuMsg.len);
            SerialSUN.flush();

            if (sunWritten != dtsuMsg.len) {
              DEBUG_PRINTF("   ❌ Failed to write to SUN2000: %u/%u bytes\n", sunWritten, dtsuMsg.len);
              systemHealth.proxyErrors++;
              reportSystemError("MODBUS", "SUN2000 write failed", sunWritten);
            }
          } else {
            DEBUG_PRINTLN("   ❌ No reply from DTSU (timeout)");
            systemHealth.proxyErrors++;
            reportSystemError("MODBUS", "DTSU timeout", 0);
          }
        } else {
          DEBUG_PRINTF("   ❌ Failed to write to DTSU: %u/%u bytes\n", written, sunMsg.len);
          systemHealth.proxyErrors++;
          reportSystemError("MODBUS", "DTSU write failed", written);
        }
        DEBUG_PRINTLN();
      }

      // Turn off NeoPixel after SUN2000 transaction completes
      pixel.setPixelColor(0, pixel.Color(0, 0, 0));
      pixel.show();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void calculatePowerCorrection() {
  float wallboxPower = 0.0f;
  bool valid = false;

  getEVCCData(sharedEVCC, wallboxPower, valid);

  if (!valid) {
    powerCorrection = 0.0f;
    powerCorrectionActive = false;
    return;
  }

  if (fabs(wallboxPower) > CORRECTION_THRESHOLD) {
    powerCorrection = wallboxPower;
    powerCorrectionActive = true;
    lastCorrectionTime = millis();
    systemHealth.lastPowerCorrection = powerCorrection;
    systemHealth.powerCorrectionActive = true;
  } else {
    if (powerCorrectionActive) {
      DEBUG_PRINTF("\n⚡ POWER CORRECTION DEACTIVATED:\n");
      DEBUG_PRINTF("   No significant wallbox charging detected\n");
    }

    powerCorrection = 0.0f;
    powerCorrectionActive = false;
    systemHealth.powerCorrectionActive = false;
  }
}

bool shouldApplyCorrection(float wallboxPower) {
  return fabs(wallboxPower) > CORRECTION_THRESHOLD;
}

void updateSharedData(const uint8_t* responseData, uint16_t responseLen, const DTSU666Data& parsedData) {
  if (xSemaphoreTake(sharedDTSU.mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
    sharedDTSU.valid = true;
    sharedDTSU.timestamp = millis();
    sharedDTSU.responseLength = responseLen;
    memcpy(sharedDTSU.responseBuffer, responseData, responseLen);
    sharedDTSU.parsedData = parsedData;
    sharedDTSU.updateCount++;
    xSemaphoreGive(sharedDTSU.mutex);
  }
}

void updateTaskHealthbeat(bool isProxyTask) {
  uint32_t currentTime = millis();
  if (isProxyTask) {
    proxyTaskLastSeen = currentTime;
  } else {
    mqttTaskLastSeen = currentTime;
  }
}

void performHealthCheck() {
  uint32_t currentTime = millis();

  if (currentTime - proxyTaskLastSeen > WATCHDOG_TIMEOUT_MS) {
    DEBUG_PRINTF("🚨 PROXY TASK TIMEOUT: %lu ms since last heartbeat\n",
                  currentTime - proxyTaskLastSeen);
    reportSystemError("WATCHDOG", "Proxy task timeout", currentTime - proxyTaskLastSeen);
  }

  if (currentTime - mqttTaskLastSeen > WATCHDOG_TIMEOUT_MS) {
    DEBUG_PRINTF("🚨 MQTT TASK TIMEOUT: %lu ms since last heartbeat\n",
                  currentTime - mqttTaskLastSeen);
    reportSystemError("WATCHDOG", "MQTT task timeout", currentTime - mqttTaskLastSeen);
  }

  systemHealth.uptime = currentTime;
  systemHealth.freeHeap = ESP.getFreeHeap();
  systemHealth.minFreeHeap = ESP.getMinFreeHeap();

  if (systemHealth.freeHeap < MIN_FREE_HEAP) {
    DEBUG_PRINTF("🚨 LOW MEMORY WARNING: %lu bytes free (threshold: %lu)\n",
                  systemHealth.freeHeap, MIN_FREE_HEAP);
    reportSystemError("MEMORY", "Low heap memory", systemHealth.freeHeap);
  }
}

void reportSystemError(const char* subsystem, const char* error, int code) {
  DEBUG_PRINTF("🚨 SYSTEM ERROR [%s]: %s", subsystem, error);
  if (code != 0) {
    DEBUG_PRINTF(" (code: %d)", code);
  }
  DEBUG_PRINTLN();
}

void watchdogTask(void *pvParameters) {
  (void)pvParameters;
  DEBUG_PRINTLN("🐕 Watchdog Task started - Independent system health monitoring");
  DEBUG_PRINTF("🐕 Running on Core %d with priority %d\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));

  while (true) {
    performHealthCheck();
    vTaskDelay(pdMS_TO_TICKS(HEALTH_CHECK_INTERVAL));
  }
}

bool isValidModbusMessage(const uint8_t* data, size_t len) {
  if (!data || len < 4) return false;
  return validateCRC(data, len);
}

bool validateCRC(const uint8_t* data, size_t len) {
  if (len < 2) return false;

  uint16_t crcGiven = (uint16_t)data[len-2] | ((uint16_t)data[len-1] << 8);
  uint16_t crcCalc = ModbusRTU485::crc16(data, len-2);

  return crcGiven == crcCalc;
}