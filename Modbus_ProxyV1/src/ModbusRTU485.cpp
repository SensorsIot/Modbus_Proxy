#include "ModbusRTU485.h"

void ModbusRTU485::begin(HardwareSerial& port, uint32_t baud) {
  _ser   = &port;
  _baud  = baud ? baud : 9600;

  // conservative: 11 bits per 8N1 char (accounts for small jitter)
  _tChar = (11UL * 1000000UL) / _baud;
  if (_tChar == 0) _tChar = 1000; // guard

  _t3_5  = (uint32_t)(3.5 * _tChar) + 2; // + tiny guard
  _t1_5  = (uint32_t)(1.5 * _tChar) + 2;
}

uint16_t ModbusRTU485::crc16(const uint8_t* p, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= p[i];
    for (uint8_t b = 0; b < 8; ++b) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else              crc >>= 1;
    }
  }
  return crc;
}

bool ModbusRTU485::parse(const uint8_t* f, uint16_t n, ModbusMessage& m) {
  m = ModbusMessage{};
  if (n < 4) return false;

  // CRC check
  uint16_t given = (uint16_t)f[n-2] | ((uint16_t)f[n-1] << 8);
  uint16_t calc  = crc16(f, n - 2);
  if (calc != given) return false;

  m.valid = true;
  m.id    = f[0];
  m.fc    = f[1];
  m.len   = n;

  // Exception?
  if ((m.fc & 0x80) && n >= 5) {
    m.type   = MBType::Exception;
    m.fc    &= 0x7F;
    m.exCode = f[2];
    return true;
  }

  switch (m.fc) {
    case 0x03: // Read Holding
    case 0x04: // Read Input
      // Either request (8 bytes total) or reply (3 + byteCount + 2 CRC)
      if (n == 8) {
        m.type      = MBType::Request;
        m.startAddr = be16(&f[2]);
        m.qty       = be16(&f[4]);
      } else {
        uint8_t bc = f[2];
        if (n == (uint16_t)bc + 5) {
          m.type      = MBType::Reply;
          m.byteCount = bc;
        } else {
          m.type = MBType::Unknown;
        }
      }
      break;

    case 0x06: // Write Single Register
      // req/rep have same format (8 bytes)
      if (n == 8) {
        m.type   = MBType::Request; // or Reply
        m.wrAddr = be16(&f[2]);
        m.wrValue= be16(&f[4]);
      } else {
        m.type = MBType::Unknown;
      }
      break;

    case 0x10: // Write Multiple Registers
      // Request: ID FC AddrHi AddrLo QtyHi QtyLo ByteCount Data... CRC
      // Reply:   ID FC AddrHi AddrLo QtyHi QtyLo CRC
      if (n == 8) {
        m.type  = MBType::Reply;
        m.wrAddr= be16(&f[2]);
        m.wrQty = be16(&f[4]);
      } else if (n >= 9) {
        uint8_t bc = f[6];
        if (n == (uint16_t)bc + 9) {
          m.type        = MBType::Request;
          m.wrAddr      = be16(&f[2]);
          m.wrQty       = be16(&f[4]);
          m.wrByteCount = bc;
        } else {
          m.type = MBType::Unknown;
        }
      } else {
        m.type = MBType::Unknown;
      }
      break;

    default:
      m.type = MBType::Unknown;
      break;
  }

  return true;
}

bool ModbusRTU485::read(ModbusMessage& out, uint32_t timeoutMs) {
  if (!_ser) return false;

  uint32_t start = millis();
  _len = 0;

  // Wait for first byte (within timeout)
  while (_ser->available() == 0) {
    if (timeoutMs && (millis() - start >= timeoutMs)) return false;
    delay(1);
  }

  // Consume a frame until inter-char gap >= 3.5T
  uint32_t lastUs = micros();
  while (true) {
    // Check overall timeout to prevent infinite loop
    if (timeoutMs && (millis() - start >= timeoutMs)) return false;

    while (_ser->available()) {
      int b = _ser->read();
      if (b < 0) break;
      if (_len < BUF_SIZE) _buf[_len++] = (uint8_t)b;
      lastUs = micros();
    }
    // Check inter-char gap
    if ((micros() - lastUs) >= _t3_5) break;
    // small idle
    delayMicroseconds(50);
  }

  // Try parse (and attach raw pointer)
  if (!parse(_buf, (uint16_t)_len, out)) return false;
  out.raw = _buf;  // expose raw frame to callers (valid until next read)
  return true;
}

bool ModbusRTU485::write(const ModbusMessage& msg, uint32_t timeoutMs) {
  if (!_ser || !msg.valid || !msg.raw) return false;
  
  // Ensure proper inter-frame gap before transmission (3.5T)
  delayMicroseconds(_t3_5);
  
  // Write the raw frame directly
  size_t written = _ser->write(msg.raw, msg.len);
  
  // Verify all bytes were written
  if (written != msg.len) return false;
  
  // Wait for transmission to complete
  _ser->flush();
  
  return true;
}

bool ModbusRTU485::write(const uint8_t* data, size_t len, uint32_t timeoutMs) {
  if (!_ser || !data || len < 2) return false;
  
  // Ensure proper inter-frame gap before transmission (3.5T)
  delayMicroseconds(_t3_5);
  
  // Calculate and append CRC if not already present
  uint8_t frame[256]; // Buffer for frame with CRC
  if (len > 254) return false; // Frame too large
  
  // Copy data to frame buffer
  memcpy(frame, data, len);
  
  // Calculate CRC for the data portion
  uint16_t crc = crc16(data, len);
  
  // Append CRC (little-endian format for MODBUS)
  frame[len] = crc & 0xFF;        // CRC low byte
  frame[len + 1] = (crc >> 8) & 0xFF; // CRC high byte
  
  // Write complete frame with CRC
  size_t totalLen = len + 2;
  size_t written = _ser->write(frame, totalLen);
  
  // Verify all bytes were written
  if (written != totalLen) return false;
  
  // Wait for transmission to complete
  _ser->flush();
  
  return true;
}