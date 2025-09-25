#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "ModbusRTU485.h"
#include "config.h"
#include "dtsu666.h"
#include "evcc_api.h"
#include "mqtt_handler.h"

// FreeRTOS task declarations
void proxyTask(void *pvParameters);
void watchdogTask(void *pvParameters);

// Proxy initialization
bool initModbusProxy();
bool initSerialInterfaces();

// Core proxy functions
void handleModbusTraffic();
bool processMessage(const ModbusMessage& msg, bool fromSUN2000);
void forwardMessage(const uint8_t* data, size_t len, bool toSUN2000);

// Power correction functions
void calculatePowerCorrection();
bool shouldApplyCorrection(float wallboxPower);
void updateSharedData(const uint8_t* responseData, uint16_t responseLen, const DTSU666Data& parsedData);

// Health monitoring
void updateTaskHealthbeat(bool isProxyTask);
void performHealthCheck();
void reportSystemError(const char* subsystem, const char* error, int code = 0);

// Message validation
bool isValidModbusMessage(const uint8_t* data, size_t len);
bool validateCRC(const uint8_t* data, size_t len);

// Task synchronization globals
extern SemaphoreHandle_t proxyTaskHealthMutex;
extern SemaphoreHandle_t mqttTaskHealthMutex;
extern uint32_t proxyTaskLastSeen;
extern uint32_t mqttTaskLastSeen;

// Serial interfaces
extern HardwareSerial SerialSUN;
extern HardwareSerial SerialDTU;

// ModbusRTU485 instances
extern ModbusRTU485 modbusSUN;
extern ModbusRTU485 modbusDTU;

// Shared data structures
extern SharedDTSUData sharedDTSU;
extern SharedEVCCData sharedEVCC;
extern DTSU666Data dtsu666Data;
extern LastRequestInfo g_lastReq;

// Power correction variables
extern float powerCorrection;
extern bool powerCorrectionActive;
extern uint32_t lastCorrectionTime;
extern uint32_t lastHttpPoll;