#pragma once
#include <Arduino.h>

enum class MBType : uint8_t {
  Unknown   = 0,
  Request   = 1,
  Reply     = 2,
  Exception = 3
};

struct ModbusMessage {
  bool     valid   = false;
  MBType   type    = MBType::Unknown;
  uint8_t  id      = 0;
  uint8_t  fc      = 0;
  uint16_t len     = 0;

  // 0x03/0x04 request
  uint16_t startAddr = 0;
  uint16_t qty       = 0;

  // 0x03/0x04 reply
  uint8_t  byteCount = 0;

  // 0x06 single write (req/rep)
  uint16_t wrAddr    = 0;
  uint16_t wrValue   = 0;

  // 0x10 multiple write
  uint16_t wrQty       = 0;
  uint8_t  wrByteCount = 0;

  // Exception data
  uint8_t  exCode    = 0;

  // Raw frame data
  const uint8_t* raw = nullptr;
};

class ModbusRTU485 {
public:
  ModbusRTU485() {}

  void begin(HardwareSerial& port, uint32_t baud);

  // Passive read with timeout (ms). Returns true if a full frame was parsed.
  bool read(ModbusMessage& out, uint32_t timeoutMs = 100);

  // Write/forward a MODBUS message. Returns true if successfully transmitted.
  bool write(const ModbusMessage& msg, uint32_t timeoutMs = 100);
  
  // Write raw frame with automatic CRC calculation. Returns true if successful.
  bool write(const uint8_t* data, size_t len, uint32_t timeoutMs = 100);

  // Public CRC calculation for response modification
  static uint16_t crc16(const uint8_t* p, size_t n);

private:
  HardwareSerial* _ser   = nullptr;
  uint32_t        _baud  = 0;
  uint32_t        _tChar = 0;   // us per 1 char (11 bits @ 8N1 safety)
  uint32_t        _t3_5  = 0;   // 3.5 char in us
  uint32_t        _t1_5  = 0;   // 1.5 char in us

  static constexpr size_t BUF_SIZE = 512;
  uint8_t _buf[BUF_SIZE];
  size_t _len = 0;

  static inline uint16_t be16(const uint8_t* p) {
    return (uint16_t(p[0]) << 8) | p[1];
  }

  bool parse(const uint8_t* f, uint16_t n, ModbusMessage& m);
};