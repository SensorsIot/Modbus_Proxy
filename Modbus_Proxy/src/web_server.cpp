#include "web_server.h"
#include "web_assets.h"
#include "wifi_manager.h"
#include "nvs_config.h"
#include "mqtt_handler.h"
#include "modbus_proxy.h"
#ifndef PRODUCTION_BUILD
#include "test_inject.h"
#endif
#include "http_ota.h"
#include "debug.h"
#include <ArduinoJson.h>

// Global instances
AsyncWebServer* webServer = nullptr;
WebServerMode currentWebMode = WEB_MODE_DISABLED;

// Forward declarations
void handleApiStatus(AsyncWebServerRequest *request);
void handleApiConfig(AsyncWebServerRequest *request);
void handleApiRestart(AsyncWebServerRequest *request);
void handleApiScan(AsyncWebServerRequest *request);

bool initWebServer(WebServerMode mode) {
  if (webServer != nullptr) {
    stopWebServer();
  }

  webServer = new AsyncWebServer(WEB_SERVER_PORT);
  if (webServer == nullptr) {
    DEBUG_PRINTLN("Failed to create web server");
    return false;
  }

  if (mode == WEB_MODE_PORTAL) {
    setupPortalRoutes();
  } else {
    setupNormalRoutes();
  }

  webServer->begin();
  currentWebMode = mode;

  DEBUG_PRINTF("Web server started in %s mode\n",
               mode == WEB_MODE_PORTAL ? "PORTAL" : "NORMAL");
  return true;
}

void stopWebServer() {
  if (webServer != nullptr) {
    webServer->end();
    delete webServer;
    webServer = nullptr;
  }
  currentWebMode = WEB_MODE_DISABLED;
  DEBUG_PRINTLN("Web server stopped");
}

bool isWebServerRunning() {
  return webServer != nullptr && currentWebMode != WEB_MODE_DISABLED;
}

WebServerMode getWebServerMode() {
  return currentWebMode;
}

void setupPortalRoutes() {
  // Serve portal page
  webServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", HTML_PORTAL_PAGE);
  });

  // WiFi scan API
  webServer->on("/api/scan", HTTP_GET, handleApiScan);

  // WiFi save API
  webServer->on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {},
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      handleApiWifi(request, data, len);
    }
  );

  // Captive portal detection endpoints
  webServer->on("/generate_204", HTTP_GET, handleCaptivePortalRedirect);
  webServer->on("/gen_204", HTTP_GET, handleCaptivePortalRedirect);
  webServer->on("/hotspot-detect.html", HTTP_GET, handleCaptivePortalRedirect);
  webServer->on("/canonical.html", HTTP_GET, handleCaptivePortalRedirect);
  webServer->on("/success.txt", HTTP_GET, handleCaptivePortalRedirect);
  webServer->on("/ncsi.txt", HTTP_GET, handleCaptivePortalRedirect);
  webServer->on("/connecttest.txt", HTTP_GET, handleCaptivePortalRedirect);
  webServer->on("/fwlink", HTTP_GET, handleCaptivePortalRedirect);

  // Catch all for captive portal
  webServer->onNotFound([](AsyncWebServerRequest *request) {
    request->send(200, "text/html", HTML_REDIRECT_PAGE);
  });
}

void setupNormalRoutes() {
  // Dashboard (main page)
  webServer->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", HTML_STATUS_PAGE);
  });

  // Status page
  webServer->on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", HTML_INFO_PAGE);
  });

  // Setup page
  webServer->on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", HTML_CONFIG_PAGE);
  });

  // Legacy config route redirect
  webServer->on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });

  // API endpoints
  webServer->on("/api/status", HTTP_GET, handleApiStatus);
  webServer->on("/api/config", HTTP_GET, handleApiConfig);

  webServer->on("/api/config", HTTP_POST, [](AsyncWebServerRequest *request) {},
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      handleApiConfigPost(request, data, len);
    }
  );

  webServer->on("/api/restart", HTTP_POST, handleApiRestart);

  webServer->on("/api/debug", HTTP_POST, [](AsyncWebServerRequest *request) {},
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      handleApiDebug(request, data, len);
    }
  );

#ifndef PRODUCTION_BUILD
  // Test injection endpoint (debug mode only)
  webServer->on("/api/test/inject", HTTP_POST, [](AsyncWebServerRequest *request) {},
    nullptr,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      handleApiTestInject(request, data, len);
    }
  );
#endif

  // HTTP OTA firmware update (POST /ota, GET /ota/health)
  setupHttpOtaRoutes(webServer);

  // 404 handler
  webServer->onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
  });
}

void handleCaptivePortalRedirect(AsyncWebServerRequest *request) {
  request->redirect("http://192.168.4.1/");
}

void handleApiStatus(AsyncWebServerRequest *request) {
  StaticJsonDocument<512> doc;

  // System info
  doc["fw_version"] = FW_VERSION;
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["min_free_heap"] = ESP.getMinFreeHeap();

  // WiFi info
  doc["wifi_connected"] = isWiFiConnected();
  doc["wifi_ssid"] = getWiFiSSID();
  doc["wifi_ip"] = getWiFiIP();
  doc["wifi_rssi"] = getWiFiRSSI();

  // MQTT info
  doc["mqtt_connected"] = mqttClient.connected();
  doc["mqtt_host"] = mqttConfig.host;
  doc["mqtt_port"] = mqttConfig.port;
  doc["mqtt_reconnects"] = systemHealth.mqttReconnects;

  // Power readings
  // sharedDTSU.parsedData already has correction applied (from proxyTask),
  // so sun2000_power is just the corrected total — do NOT add powerCorrection again.
  float correctedTotal = sharedDTSU.valid ? sharedDTSU.parsedData.power_total : 0.0f;
  float originalDtsu = sharedDTSU.valid ?
    (correctedTotal - (powerCorrectionActive ? powerCorrection : 0.0f)) : 0.0f;
  doc["dtsu_power"] = originalDtsu;
  doc["wallbox_power"] = powerCorrection;
  doc["sun2000_power"] = correctedTotal;
  doc["correction_active"] = powerCorrectionActive;

  // Statistics
  doc["dtsu_updates"] = systemHealth.dtsuUpdates;
  doc["wallbox_updates"] = systemHealth.wallboxUpdates;
  doc["wallbox_errors"] = systemHealth.wallboxErrors;
  doc["proxy_errors"] = systemHealth.proxyErrors;

  // Debug mode
  doc["debug_mode"] = isDebugModeEnabled();

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void handleApiConfig(AsyncWebServerRequest *request) {
  StaticJsonDocument<256> doc;

  doc["mqtt_host"] = mqttConfig.host;
  doc["mqtt_port"] = mqttConfig.port;
  doc["mqtt_user"] = mqttConfig.user;
  doc["wallbox_topic"] = mqttConfig.wallboxTopic;
  doc["log_level"] = mqttConfig.logLevel;

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void handleApiConfigPost(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }

  const char* type = doc["type"] | "";
  StaticJsonDocument<128> response;

  if (strcmp(type, "mqtt") == 0) {
    const char* host = doc["host"] | "";
    uint16_t port = doc["port"] | 1883;
    const char* user = doc["user"] | "";
    const char* pass = doc["pass"] | "";

    if (saveMQTTCredentials(host, port, user, pass)) {
      response["status"] = "ok";
      triggerMqttReconnect();
    } else {
      response["status"] = "error";
      response["message"] = "Failed to save";
    }
  }
  else if (strcmp(type, "wallbox") == 0) {
    const char* topic = doc["topic"] | "";
    if (saveWallboxTopic(topic)) {
      response["status"] = "ok";
      triggerMqttReconnect();
    } else {
      response["status"] = "error";
      response["message"] = "Failed to save";
    }
  }
  else if (strcmp(type, "loglevel") == 0) {
    uint8_t level = doc["level"] | 2;
    if (saveLogLevel(level)) {
      response["status"] = "ok";
    } else {
      response["status"] = "error";
      response["message"] = "Invalid level";
    }
  }
  else if (strcmp(type, "reset") == 0) {
    if (resetToDefaults()) {
      response["status"] = "ok";
      // Schedule restart
      delay(500);
      ESP.restart();
    } else {
      response["status"] = "error";
      response["message"] = "Reset failed";
    }
  }
  else {
    response["status"] = "error";
    response["message"] = "Unknown type";
  }

  String responseStr;
  serializeJson(response, responseStr);
  request->send(200, "application/json", responseStr);
}

void handleApiRestart(AsyncWebServerRequest *request) {
  request->send(200, "application/json", "{\"status\":\"ok\"}");
  delay(500);
  ESP.restart();
}

void handleApiDebug(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  StaticJsonDocument<64> doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    request->send(400, "application/json", "{\"status\":\"error\"}");
    return;
  }

  bool enabled = doc["enabled"] | false;
  setDebugMode(enabled);

  request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleApiScan(AsyncWebServerRequest *request) {
  WiFiScanResult results[10];
  int count = scanWiFiNetworks(results, 10);

  StaticJsonDocument<1024> doc;
  JsonArray networks = doc.createNestedArray("networks");

  for (int i = 0; i < count; i++) {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = results[i].ssid;
    network["rssi"] = results[i].rssi;
    network["encrypted"] = results[i].encryptionType != WIFI_AUTH_OPEN;
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response);
}

void handleApiWifi(AsyncWebServerRequest *request, uint8_t *data, size_t len) {
  DEBUG_PRINTF("[WIFI-API] Body received: %d bytes\n", (int)len);
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, data, len);

  if (error) {
    DEBUG_PRINTF("[WIFI-API] JSON parse error: %s\n", error.c_str());
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    return;
  }

  const char* ssid = doc["ssid"] | "";
  const char* password = doc["password"] | "";
  DEBUG_PRINTF("[WIFI-API] SSID='%s', pass len=%d\n", ssid, (int)strlen(password));

  if (strlen(ssid) == 0) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SSID required\"}");
    return;
  }

  if (saveWiFiCredentials(ssid, password)) {
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    DEBUG_PRINTLN("WiFi credentials saved via portal, restarting...");
    delay(1000);
    ESP.restart();
  } else {
    DEBUG_PRINTLN("[WIFI-API] saveWiFiCredentials FAILED");
    request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
  }
}
