#pragma once

#include "Arduino.h"

#include <map>
#include <string>

// Host-side stand-in for the ESP32 Preferences library.
//
// Two behaviours matter for the code under test:
//   * values survive begin()/end(), so save/load round-trips work
//   * the put* calls return the number of BYTES WRITTEN, exactly like the real
//     library, so writing an empty string returns 0 rather than an error
class Preferences {
public:
  // Wipes every namespace. Call from setUp() so tests don't leak state.
  static void resetAll() { store().clear(); }

  bool begin(const char* name, bool readOnly = false) {
    _ns = name ? name : "";
    _readOnly = readOnly;
    return true;
  }
  void end() {}

  bool clear() {
    if (_readOnly) return false;
    const std::string prefix = _ns + ":";
    for (auto it = store().begin(); it != store().end();) {
      it = (it->first.rfind(prefix, 0) == 0) ? store().erase(it) : std::next(it);
    }
    return true;
  }

  String getString(const char* key, const char* def = "") {
    auto it = store().find(path(key));
    return String(it == store().end() ? (def ? def : "") : it->second.c_str());
  }

  size_t putString(const char* key, const char* value) {
    if (_readOnly || value == nullptr) return 0;
    store()[path(key)] = value;
    return strlen(value);
  }
  size_t putString(const char* key, const String& value) {
    return putString(key, value.c_str());
  }

  uint16_t getUShort(const char* key, uint16_t def = 0) {
    return static_cast<uint16_t>(getNum(key, def));
  }
  size_t putUShort(const char* key, uint16_t value) {
    return putNum(key, value, sizeof(uint16_t));
  }

  uint8_t getUChar(const char* key, uint8_t def = 0) {
    return static_cast<uint8_t>(getNum(key, def));
  }
  size_t putUChar(const char* key, uint8_t value) {
    return putNum(key, value, sizeof(uint8_t));
  }

  bool getBool(const char* key, bool def = false) {
    return getNum(key, def ? 1 : 0) != 0;
  }
  size_t putBool(const char* key, bool value) {
    return putNum(key, value ? 1 : 0, sizeof(uint8_t));
  }

private:
  static std::map<std::string, std::string>& store() {
    static std::map<std::string, std::string> s;
    return s;
  }

  std::string path(const char* key) const {
    return _ns + ":" + (key ? key : "");
  }

  unsigned long getNum(const char* key, unsigned long def) {
    auto it = store().find(path(key));
    return it == store().end() ? def : strtoul(it->second.c_str(), nullptr, 10);
  }

  size_t putNum(const char* key, unsigned long value, size_t width) {
    if (_readOnly) return 0;
    store()[path(key)] = std::to_string(value);
    return width;
  }

  std::string _ns;
  bool _readOnly = false;
};
