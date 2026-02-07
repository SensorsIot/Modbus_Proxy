#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "config.h"
#include "nvs_config.h"

// WiFi connection states
enum WiFiState {
  WIFI_STATE_DISCONNECTED = 0,
  WIFI_STATE_CONNECTING,
  WIFI_STATE_CONNECTED,
  WIFI_STATE_AP_MODE,
  WIFI_STATE_PORTAL_ACTIVE
};

// WiFi scan result
struct WiFiScanResult {
  char ssid[33];
  int32_t rssi;
  uint8_t encryptionType;
};

// Function declarations
bool initWiFiManager();
WiFiState connectWiFi(uint32_t timeoutMs = WIFI_CONNECT_TIMEOUT_MS);
bool enterCaptivePortalMode();
void exitCaptivePortalMode();
bool isCaptivePortalActive();
void handleCaptivePortalDNS();

// WiFi scanning
int scanWiFiNetworks(WiFiScanResult* results, int maxResults);
bool isWiFiConnected();
int getWiFiRSSI();
String getWiFiSSID();
String getWiFiIP();

// Portal state
extern bool captivePortalActive;
extern uint32_t captivePortalStartTime;
extern DNSServer* dnsServer;
