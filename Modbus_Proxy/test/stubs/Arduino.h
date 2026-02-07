#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

// Arduino types
typedef uint8_t byte;
typedef bool boolean;

// Pin modes and values
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2

// Serial config
#define SERIAL_8N1 0x800001c

// No-op functions
inline void pinMode(uint8_t, uint8_t) {}
inline void digitalWrite(uint8_t, uint8_t) {}
inline int digitalRead(uint8_t) { return 0; }
inline unsigned long millis() { return 0; }
inline unsigned long micros() { return 0; }
inline void delay(unsigned long) {}
inline void delayMicroseconds(unsigned int) {}
inline void yield() {}

// String class wrapping std::string
class String {
public:
  String() : _str() {}
  String(const char* s) : _str(s ? s : "") {}
  String(const std::string& s) : _str(s) {}
  String(int val) : _str(std::to_string(val)) {}
  String(unsigned int val) : _str(std::to_string(val)) {}
  String(long val) : _str(std::to_string(val)) {}
  String(unsigned long val) : _str(std::to_string(val)) {}
  String(float val, unsigned int decimalPlaces = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimalPlaces, val);
    _str = buf;
  }
  String(double val, unsigned int decimalPlaces = 2) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.*f", decimalPlaces, val);
    _str = buf;
  }

  const char* c_str() const { return _str.c_str(); }
  unsigned int length() const { return _str.length(); }
  bool isEmpty() const { return _str.empty(); }

  String& operator=(const char* s) { _str = s ? s : ""; return *this; }
  String& operator+=(const char* s) { if (s) _str += s; return *this; }
  String& operator+=(const String& s) { _str += s._str; return *this; }
  String operator+(const String& s) const { return String(_str + s._str); }
  bool operator==(const char* s) const { return _str == (s ? s : ""); }
  bool operator==(const String& s) const { return _str == s._str; }
  bool operator!=(const char* s) const { return !(*this == s); }
  char operator[](unsigned int i) const { return _str[i]; }

  int indexOf(char c) const { auto p = _str.find(c); return p == std::string::npos ? -1 : (int)p; }

  friend String operator+(const char* lhs, const String& rhs) {
    return String(std::string(lhs ? lhs : "") + rhs._str);
  }

private:
  std::string _str;
};

// SerialStub
class SerialStub {
public:
  void begin(unsigned long) {}
  void begin(unsigned long, uint32_t) {}
  void begin(unsigned long, uint32_t, int8_t, int8_t) {}
  void end() {}
  int available() { return 0; }
  int read() { return -1; }
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t* buf, size_t len) { return len; }
  void flush() {}
  void print(const char*) {}
  void print(int) {}
  void print(unsigned int) {}
  void print(float) {}
  void println() {}
  void println(const char*) {}
  void println(int) {}
  void println(const String&) {}
  void printf(const char*, ...) {}
  operator bool() const { return true; }
};

extern SerialStub Serial;

// IPAddress stub
class IPAddress {
public:
  IPAddress() : _addr(0) {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
    : _addr(((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | d) {}
  String toString() const { return String("0.0.0.0"); }
  operator uint32_t() const { return _addr; }
private:
  uint32_t _addr;
};

// ESP stub
class ESPClass {
public:
  uint32_t getFreeHeap() { return 200000; }
  uint32_t getMinFreeHeap() { return 150000; }
  void restart() {}
};
inline ESPClass ESP;

// Pull in related Arduino headers (as real Arduino.h does)
#include "HardwareSerial.h"
#include "freertos/FreeRTOS.h"
