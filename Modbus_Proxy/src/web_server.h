#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "config.h"

// Web server modes
enum WebServerMode {
  WEB_MODE_DISABLED = 0,
  WEB_MODE_PORTAL,      // Captive portal mode (AP)
  WEB_MODE_NORMAL       // Normal STA mode
};

// Function declarations
bool initWebServer(WebServerMode mode);
void stopWebServer();
bool isWebServerRunning();
WebServerMode getWebServerMode();

// Route handlers (internal)
void setupPortalRoutes();
void setupNormalRoutes();

// API handlers
void handleApiStatus(AsyncWebServerRequest *request);
void handleApiConfig(AsyncWebServerRequest *request);
void handleApiConfigPost(AsyncWebServerRequest *request, uint8_t *data, size_t len);
void handleApiRestart(AsyncWebServerRequest *request);
void handleApiDebug(AsyncWebServerRequest *request, uint8_t *data, size_t len);
void handleApiScan(AsyncWebServerRequest *request);
void handleApiWifi(AsyncWebServerRequest *request, uint8_t *data, size_t len);

// Portal redirect handler
void handleCaptivePortalRedirect(AsyncWebServerRequest *request);

// Global web server instance
extern AsyncWebServer* webServer;
extern WebServerMode currentWebMode;
