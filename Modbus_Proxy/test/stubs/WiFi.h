#pragma once
#include "Arduino.h"

#define WIFI_AUTH_OPEN 0

class WiFiClass {
public:
  String macAddress() { return String("AA:BB:CC:DD:EE:FF"); }
  bool isConnected() { return false; }
  int32_t RSSI() { return -50; }
  IPAddress localIP() { return IPAddress(192, 168, 0, 177); }
};

extern WiFiClass WiFi;
