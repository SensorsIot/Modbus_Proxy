#pragma once
#include "Arduino.h"

class HardwareSerial : public SerialStub {
public:
  HardwareSerial(int num = 0) {}
};
