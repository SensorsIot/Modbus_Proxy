#pragma once

#include <ESPAsyncWebServer.h>

// Register HTTP OTA routes on the given web server
// Adds: POST /ota (firmware upload), GET /ota/health
void setupHttpOtaRoutes(AsyncWebServer* server);
