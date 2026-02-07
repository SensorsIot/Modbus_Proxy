#include "http_ota.h"
#include "debug.h"
#include <Update.h>

static const char* OTA_PASSWORD = "modbus_ota_2023";

static bool checkOtaAuth(AsyncWebServerRequest *request) {
  if (!request->hasHeader("Authorization")) {
    request->send(401, "application/json", "{\"status\":\"error\",\"message\":\"Authorization required\"}");
    return false;
  }
  String auth = request->header("Authorization");
  String expected = String("Bearer ") + OTA_PASSWORD;
  if (auth != expected) {
    DEBUG_PRINTLN("[HTTP OTA] Auth failed");
    request->send(403, "application/json", "{\"status\":\"error\",\"message\":\"Invalid credentials\"}");
    return false;
  }
  return true;
}

void setupHttpOtaRoutes(AsyncWebServer* server) {
  // Health check endpoint (no auth needed)
  server->on("/ota/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // Firmware upload endpoint
  server->on("/ota", HTTP_POST,
    // Request complete handler — called after all data received
    [](AsyncWebServerRequest *request) {
      bool success = !Update.hasError();
      AsyncWebServerResponse *response = request->beginResponse(
        success ? 200 : 500,
        "application/json",
        success ? "{\"status\":\"ok\",\"message\":\"Rebooting...\"}"
                : "{\"status\":\"error\",\"message\":\"Update failed\"}"
      );
      response->addHeader("Connection", "close");
      request->send(response);
      if (success) {
        DEBUG_PRINTLN("[HTTP OTA] Success, rebooting...");
        delay(500);
        ESP.restart();
      }
    },
    // Upload handler — called for each chunk
    [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
      if (index == 0) {
        // Check auth on first chunk
        if (!request->hasHeader("Authorization")) {
          DEBUG_PRINTLN("[HTTP OTA] No auth header, rejecting");
          return;
        }
        String auth = request->header("Authorization");
        String expected = String("Bearer ") + OTA_PASSWORD;
        if (auth != expected) {
          DEBUG_PRINTLN("[HTTP OTA] Auth failed, rejecting");
          return;
        }
        DEBUG_PRINTF("[HTTP OTA] Receiving: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          DEBUG_PRINTF("[HTTP OTA] Begin failed: %s\n", Update.errorString());
        }
      }
      if (Update.isRunning()) {
        if (Update.write(data, len) != len) {
          DEBUG_PRINTF("[HTTP OTA] Write failed: %s\n", Update.errorString());
        }
      }
      if (final) {
        if (Update.end(true)) {
          DEBUG_PRINTF("[HTTP OTA] Complete: %u bytes\n", index + len);
        } else {
          DEBUG_PRINTF("[HTTP OTA] End failed: %s\n", Update.errorString());
        }
      }
    }
  );

  DEBUG_PRINTLN("[HTTP OTA] Routes registered (POST /ota, GET /ota/health)");
}
