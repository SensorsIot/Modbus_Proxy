#include "modbus_proxy.h"
#include "debug.h"
#include "mqtt_logger.h"
#include <esp_task_wdt.h>

// Serial interfaces
// ESP32-C3 only has UART0 and UART1 (UART0 for debug, UART1 for RS485)
// We'll use UART0 for SUN2000 and UART1 for DTU
HardwareSerial SerialSUN(0);   // UART0 for SUN2000
HardwareSerial SerialDTU(1);   // UART1 for DTU

// ModbusRTU485 instances
ModbusRTU485 modbusSUN;
ModbusRTU485 modbusDTU;

// Shared data structures
SharedDTSUData sharedDTSU = {NULL, false, 0, {}, 0, {}, 0};
DTSU666Data dtsu666Data;
LastRequestInfo g_lastReq;

// Power correction variables
float powerCorrection = 0.0f;
bool powerCorrectionActive = false;
uint32_t lastCorrectionTime = 0;

// Task synchronization
SemaphoreHandle_t proxyTaskHealthMutex;
SemaphoreHandle_t mqttTaskHealthMutex;
uint32_t proxyTaskLastSeen = 0;
uint32_t mqttTaskLastSeen = 0;

bool initModbusProxy() {
  proxyTaskHealthMutex = xSemaphoreCreateMutex();
  mqttTaskHealthMutex = xSemaphoreCreateMutex();

  if (!proxyTaskHealthMutex || !mqttTaskHealthMutex) {
    DEBUG_PRINTLN("Failed to create health monitoring mutexes");
    return false;
  }

  sharedDTSU.mutex = xSemaphoreCreateMutex();
  if (!sharedDTSU.mutex) {
    DEBUG_PRINTLN("Failed to create DTSU data mutex");
    return false;
  }

  return initSerialInterfaces();
}

bool initSerialInterfaces() {
  DEBUG_PRINTLN("Initializing RS485 interfaces...");

  SerialSUN.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);
  DEBUG_PRINTF("   SUN2000 interface: UART2, %d baud, pins %d(RX)/%d(TX)\n",
                MODBUS_BAUDRATE, RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);

  SerialDTU.begin(MODBUS_BAUDRATE, SERIAL_8N1, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);
  DEBUG_PRINTF("   DTSU-666 interface: UART1, %d baud, pins %d(RX)/%d(TX)\n",
                MODBUS_BAUDRATE, RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);

  modbusSUN.begin(SerialSUN, MODBUS_BAUDRATE);
  DEBUG_PRINTLN("   SUN2000 MODBUS handler initialized");

  modbusDTU.begin(SerialDTU, MODBUS_BAUDRATE);
  DEBUG_PRINTLN("   DTSU-666 MODBUS handler initialized");

  return true;
}


void proxyTask(void *pvParameters) {
  DEBUG_PRINTLN("Simple Proxy Task started - Direct SUN2000 <-> DTSU proxying");

  ModbusMessage sunMsg;
  uint32_t proxyCount = 0;

  DEBUG_PRINTLN("\nMODBUS PROXY DEBUG MODE ACTIVE");
  DEBUG_PRINTF("   SUN2000 interface: RX=GPIO%d, TX=GPIO%d\n", RS485_SUN2000_RX_PIN, RS485_SUN2000_TX_PIN);
  DEBUG_PRINTF("   DTU interface: RX=GPIO%d, TX=GPIO%d\n", RS485_DTU_RX_PIN, RS485_DTU_TX_PIN);
  DEBUG_PRINTLN("   Waiting for MODBUS traffic...\n");

  uint32_t lastDebugTime = 0;
  uint32_t noTrafficCount = 0;

  while (true) {
    updateTaskHealthbeat(true);

    // Periodic status report every 10 seconds
    if (millis() - lastDebugTime > 10000) {
      DEBUG_PRINTF("No MODBUS traffic for %lu seconds (waiting on SUN2000 RX=GPIO%d)\n",
                   noTrafficCount * 10, RS485_SUN2000_RX_PIN);
      lastDebugTime = millis();
      noTrafficCount++;
    }

    if (modbusSUN.read(sunMsg, 2000)) {
      // Reset no-traffic counter
      noTrafficCount = 0;
      lastDebugTime = millis();

      // Flash LED to indicate SUN2000 interface activity
      LED_ON();

      proxyCount++;
      uint32_t proxyStart = millis();

      // Only process and debug DTSU (ID=11) messages
      if (sunMsg.id == 11 && sunMsg.type == MBType::Request) {
        uint32_t dtsuStart = millis();
        size_t written = SerialDTU.write(sunMsg.raw, sunMsg.len);
        SerialDTU.flush();

        if (written == sunMsg.len) {
          ModbusMessage dtsuMsg;
          if (modbusDTU.read(dtsuMsg, 1000)) {
            uint32_t dtsuTime = millis() - dtsuStart;

            if (dtsuMsg.type == MBType::Exception) {
              DEBUG_PRINTF("   DTSU EXCEPTION: Code=0x%02X\n", dtsuMsg.exCode);
              systemHealth.proxyErrors++;
              reportSystemError("MODBUS", "DTSU exception", dtsuMsg.exCode);
            } else if (dtsuMsg.fc == 0x03 && dtsuMsg.len >= 165) {
              DTSU666Data dtsuData;
              if (parseDTSU666Response(dtsuMsg.raw, dtsuMsg.len, dtsuData)) {
                dtsu666Data = dtsuData;
                calculateProxyPowerCorrection();

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

                float currentWallboxPower = getWallboxPower();

                float sun2000Value = dtsuData.power_total;
                if (correctionAppliedSuccessfully && powerCorrection > 0) {
                  sun2000Value = finalData.power_total;
                }

                // Single line debug output with all three values
                DEBUG_PRINTF("DTSU: %.1fW | Wallbox: %.1fW | SUN2000: %.1fW (%.1fW %c %.1fW)\n",
                            dtsuData.power_total,
                            currentWallboxPower,
                            sun2000Value,
                            dtsuData.power_total,
                            correctionAppliedSuccessfully && powerCorrection >= 0 ? '+' : '-',
                            fabs(correctionAppliedSuccessfully ? powerCorrection : 0.0f));

                updateSharedData(dtsuMsg.raw, dtsuMsg.len, finalData);
                systemHealth.dtsuUpdates++;
              }
            }

            // Send response to SUN2000
            size_t sunWritten = SerialSUN.write(dtsuMsg.raw, dtsuMsg.len);
            SerialSUN.flush();

            if (sunWritten != dtsuMsg.len) {
              DEBUG_PRINTF("   Failed to write to SUN2000: %u/%u bytes\n", sunWritten, dtsuMsg.len);
              systemHealth.proxyErrors++;
              reportSystemError("MODBUS", "SUN2000 write failed", sunWritten);
            }
          } else {
            DEBUG_PRINTLN("DTSU TIMEOUT");
            systemHealth.proxyErrors++;
            reportSystemError("MODBUS", "DTSU timeout", 0);
          }
        } else {
          DEBUG_PRINTLN("DTSU WRITE FAILED");
          systemHealth.proxyErrors++;
          reportSystemError("MODBUS", "DTSU write failed", written);
        }
      }

      // Turn off LED after SUN2000 transaction completes
      LED_OFF();
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void calculateProxyPowerCorrection() {
  float wallboxPower = 0.0f;
  bool valid = false;

  getWallboxData(wallboxPower, valid);

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
      DEBUG_PRINTF("\nPOWER CORRECTION DEACTIVATED:\n");
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
  bool criticalFailure = false;

  if (currentTime - proxyTaskLastSeen > WATCHDOG_TIMEOUT_MS) {
    DEBUG_PRINTF("PROXY TASK TIMEOUT: %lu ms since last heartbeat\n",
                  currentTime - proxyTaskLastSeen);
    MLOG_ERROR("WATCHDOG", "Proxy task timeout (%lums) - triggering reboot", currentTime - proxyTaskLastSeen);
    reportSystemError("WATCHDOG", "Proxy task timeout", currentTime - proxyTaskLastSeen);
    criticalFailure = true;
  }

  if (currentTime - mqttTaskLastSeen > WATCHDOG_TIMEOUT_MS) {
    DEBUG_PRINTF("MQTT TASK TIMEOUT: %lu ms since last heartbeat\n",
                  currentTime - mqttTaskLastSeen);
    MLOG_ERROR("WATCHDOG", "MQTT task timeout (%lums) - triggering reboot", currentTime - mqttTaskLastSeen);
    reportSystemError("WATCHDOG", "MQTT task timeout", currentTime - mqttTaskLastSeen);
    criticalFailure = true;
  }

  systemHealth.uptime = currentTime;
  systemHealth.freeHeap = ESP.getFreeHeap();
  systemHealth.minFreeHeap = ESP.getMinFreeHeap();

  if (systemHealth.freeHeap < MIN_FREE_HEAP) {
    DEBUG_PRINTF("LOW MEMORY WARNING: %lu bytes free (threshold: %lu)\n",
                  systemHealth.freeHeap, MIN_FREE_HEAP);
    MLOG_WARN("MEMORY", "Low heap: %lu bytes (threshold: %lu)", systemHealth.freeHeap, MIN_FREE_HEAP);
    reportSystemError("MEMORY", "Low heap memory", systemHealth.freeHeap);
  }

  // Critical memory threshold - reboot if heap is critically low
  if (systemHealth.freeHeap < (MIN_FREE_HEAP / 2)) {
    MLOG_ERROR("MEMORY", "Critical heap: %lu bytes - triggering reboot", systemHealth.freeHeap);
    criticalFailure = true;
  }

  // Trigger reboot on critical failure
  if (criticalFailure) {
    DEBUG_PRINTLN("!!! CRITICAL FAILURE DETECTED - REBOOTING IN 2 SECONDS !!!");
    delay(2000);  // Allow time for log message to be sent
    ESP.restart();
  }
}

void reportSystemError(const char* subsystem, const char* error, int code) {
  DEBUG_PRINTF("SYSTEM ERROR [%s]: %s", subsystem, error);
  if (code != 0) {
    DEBUG_PRINTF(" (code: %d)", code);
  }
  DEBUG_PRINTLN();
}

void watchdogTask(void *pvParameters) {
  (void)pvParameters;
  DEBUG_PRINTLN("Watchdog Task started - Independent system health monitoring");
  DEBUG_PRINTF("Running on Core %d with priority %d\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));

  // Initialize hardware watchdog timer (90 second timeout)
  // This is a safety net in case the watchdog task itself hangs
  esp_task_wdt_init(90, true);  // 90 second timeout, panic on timeout
  esp_task_wdt_add(NULL);  // Add current task to watchdog
  MLOG_INFO("WATCHDOG", "Hardware WDT initialized (90s timeout)");

  while (true) {
    // Feed the hardware watchdog
    esp_task_wdt_reset();

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
