#pragma once

#include <Arduino.h>
#include "config.h"

// Telnet debug support
#if defined(ENABLE_TELNET_DEBUG) && ENABLE_TELNET_DEBUG
  #include <WiFi.h>

  class TelnetDebug {
  private:
    WiFiServer* server;
    WiFiClient client;
    char buffer[512];
    size_t bufferPos;

  public:
    TelnetDebug() : server(nullptr), bufferPos(0) {}

    void begin(uint16_t port = 23) {
      if (!server) {
        server = new WiFiServer(port);
        server->begin();
        server->setNoDelay(true);
      }
    }

    void handle() {
      if (server && server->hasClient()) {
        if (!client || !client.connected()) {
          if (client) client.stop();
          client = server->available();
          client.setNoDelay(true);
          client.println("\n=== ESP32-S3 MODBUS Proxy Debug ===");
          client.println("Telnet debug session started");
          client.println("Type 'help' for commands (not implemented yet)\n");
        }
      }
    }

    void print(const char* str) {
      if (client && client.connected()) {
        client.print(str);
      }
    }

    void println(const char* str) {
      if (client && client.connected()) {
        client.println(str);
      }
    }

    void println() {
      if (client && client.connected()) {
        client.println();
      }
    }

    void printf(const char* format, ...) {
      if (client && client.connected()) {
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        client.print(buffer);
      }
    }
  };

  extern TelnetDebug telnetDebug;

  #define DEBUG_PRINT(x) telnetDebug.print(x)
  #define DEBUG_PRINTLN(x) telnetDebug.println(x)
  #define DEBUG_PRINTF(...) telnetDebug.printf(__VA_ARGS__)
  #define DEBUG_HANDLE() telnetDebug.handle()
#elif ENABLE_SERIAL_DEBUG
  #define DEBUG_PRINT(x) Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
  #define DEBUG_HANDLE()
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
  #define DEBUG_HANDLE()
#endif
