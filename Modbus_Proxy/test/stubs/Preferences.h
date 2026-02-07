#pragma once
#include "Arduino.h"

class Preferences {
public:
  bool begin(const char*, bool = false) { return true; }
  void end() {}
  bool clear() { return true; }

  String getString(const char*, const char* def = "") { return String(def); }
  bool putString(const char*, const char*) { return true; }
  bool putString(const char*, const String&) { return true; }

  uint16_t getUShort(const char*, uint16_t def = 0) { return def; }
  bool putUShort(const char*, uint16_t) { return true; }

  uint8_t getUChar(const char*, uint8_t def = 0) { return def; }
  bool putUChar(const char*, uint8_t) { return true; }

  bool getBool(const char*, bool def = false) { return def; }
  bool putBool(const char*, bool) { return true; }
};
