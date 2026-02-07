#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// Test injection endpoint handler (debug mode only)
// Simulates DTSU meter data flowing through the proxy pipeline
void handleApiTestInject(AsyncWebServerRequest *request, uint8_t *data, size_t len);
