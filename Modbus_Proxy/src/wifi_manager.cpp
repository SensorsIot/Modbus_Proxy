#include "wifi_manager.h"
#include "credentials.h"
#include "debug.h"

// Global state
bool captivePortalActive = false;
uint32_t captivePortalStartTime = 0;
DNSServer* dnsServer = nullptr;

bool initWiFiManager() {
  WiFi.setSleep(false);
  WiFi.setHostname("MODBUS-Proxy");
  return true;
}

WiFiState connectWiFi(uint32_t timeoutMs) {
  char nvsSSID[64] = {0};
  char nvsPass[64] = {0};

  // Try NVS credentials first
  if (loadWiFiCredentials(nvsSSID, sizeof(nvsSSID), nvsPass, sizeof(nvsPass))) {
    DEBUG_PRINTF("Trying NVS WiFi credentials: SSID=%s\n", nvsSSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(nvsSSID, nvsPass);

    uint32_t startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
      LED_ON();
      delay(250);
      LED_OFF();
      delay(250);
      DEBUG_PRINTF("[%d]", WiFi.status());
    }

    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN();
      DEBUG_PRINTF("WiFi connected via NVS! IP: %s\n", WiFi.localIP().toString().c_str());
      return WIFI_STATE_CONNECTED;
    }

    DEBUG_PRINTLN();
    DEBUG_PRINTLN("NVS WiFi credentials failed, trying fallback...");
    WiFi.disconnect();
    delay(100);
  }

  // Fallback to credentials.h
  DEBUG_PRINTF("Trying fallback WiFi credentials: SSID=%s\n", ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  uint32_t startTime = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
    LED_ON();
    delay(250);
    LED_OFF();
    delay(250);
    DEBUG_PRINTF("[%d]", WiFi.status());
  }

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN();
    DEBUG_PRINTF("WiFi connected via fallback! IP: %s\n", WiFi.localIP().toString().c_str());
    return WIFI_STATE_CONNECTED;
  }

  DEBUG_PRINTLN();
  DEBUG_PRINTLN("WiFi connection failed");
  return WIFI_STATE_DISCONNECTED;
}

bool enterCaptivePortalMode() {
  DEBUG_PRINTLN("Entering captive portal mode...");

  // Disconnect from any existing connection
  WiFi.disconnect();
  delay(100);

  // Configure AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(CAPTIVE_PORTAL_IP, CAPTIVE_PORTAL_GATEWAY, CAPTIVE_PORTAL_SUBNET);

  if (!WiFi.softAP(CAPTIVE_PORTAL_SSID)) {
    DEBUG_PRINTLN("Failed to start AP");
    return false;
  }

  DEBUG_PRINTF("AP Started: SSID=%s, IP=%s\n",
               CAPTIVE_PORTAL_SSID,
               WiFi.softAPIP().toString().c_str());

  // Start DNS server to redirect all requests
  if (dnsServer == nullptr) {
    dnsServer = new DNSServer();
  }
  dnsServer->start(53, "*", CAPTIVE_PORTAL_IP);

  captivePortalActive = true;
  captivePortalStartTime = millis();

  // Blink LED to indicate portal mode
  for (int i = 0; i < 10; i++) {
    LED_ON();
    delay(100);
    LED_OFF();
    delay(100);
  }

  DEBUG_PRINTLN("Captive portal active");
  return true;
}

void exitCaptivePortalMode() {
  DEBUG_PRINTLN("Exiting captive portal mode...");

  if (dnsServer != nullptr) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }

  WiFi.softAPdisconnect(true);
  captivePortalActive = false;

  DEBUG_PRINTLN("Captive portal stopped");
}

bool isCaptivePortalActive() {
  return captivePortalActive;
}

void handleCaptivePortalDNS() {
  if (dnsServer != nullptr && captivePortalActive) {
    dnsServer->processNextRequest();
  }
}

int scanWiFiNetworks(WiFiScanResult* results, int maxResults) {
  DEBUG_PRINTLN("Starting WiFi scan...");

  int numNetworks = WiFi.scanNetworks(false, false, false, 300);

  if (numNetworks < 0) {
    DEBUG_PRINTF("WiFi scan failed: %d\n", numNetworks);
    return 0;
  }

  int count = min(numNetworks, maxResults);

  for (int i = 0; i < count; i++) {
    strncpy(results[i].ssid, WiFi.SSID(i).c_str(), sizeof(results[i].ssid) - 1);
    results[i].ssid[sizeof(results[i].ssid) - 1] = '\0';
    results[i].rssi = WiFi.RSSI(i);
    results[i].encryptionType = WiFi.encryptionType(i);
  }

  WiFi.scanDelete();

  DEBUG_PRINTF("WiFi scan found %d networks\n", count);
  return count;
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

int getWiFiRSSI() {
  if (!isWiFiConnected()) {
    return 0;
  }
  return WiFi.RSSI();
}

String getWiFiSSID() {
  if (!isWiFiConnected()) {
    return "";
  }
  return WiFi.SSID();
}

String getWiFiIP() {
  if (!isWiFiConnected()) {
    return "";
  }
  return WiFi.localIP().toString();
}
