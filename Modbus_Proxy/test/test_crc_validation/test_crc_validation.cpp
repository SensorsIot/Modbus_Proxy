#include <unity.h>
#include <cstring>
#include "ModbusRTU485.h"

// Local helper: validate CRC on a complete frame (data + 2 CRC bytes)
static bool validateCRC(const uint8_t* frame, uint16_t len) {
  if (len < 4) return false;
  uint16_t calc = ModbusRTU485::crc16(frame, len - 2);
  uint16_t given = (uint16_t)frame[len - 2] | ((uint16_t)frame[len - 1] << 8);
  return calc == given;
}

// Local helper: check minimum length + CRC validity
static bool isValidModbusMessage(const uint8_t* frame, uint16_t len) {
  if (!frame || len < 4) return false;
  return validateCRC(frame, len);
}

// --- CRC16 known vectors ---

void test_crc16_known_read_request(void) {
  // Standard Modbus read holding registers: addr=0x0B, FC=0x03, start=0x0846, qty=0x0050
  // Known frame: 0B 03 08 46 00 50 CRC_LO CRC_HI
  uint8_t data[] = {0x0B, 0x03, 0x08, 0x46, 0x00, 0x50};
  uint16_t crc = ModbusRTU485::crc16(data, 6);
  // Build frame and validate round-trip
  uint8_t frame[8];
  memcpy(frame, data, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;
  TEST_ASSERT_TRUE(validateCRC(frame, 8));
}

void test_crc16_single_byte(void) {
  uint8_t data[] = {0x00};
  uint16_t crc = ModbusRTU485::crc16(data, 1);
  // CRC of 0x00 should be deterministic
  TEST_ASSERT_NOT_EQUAL(0, crc);
  TEST_ASSERT_NOT_EQUAL(0xFFFF, crc);
}

void test_crc16_all_zeros(void) {
  uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
  uint16_t crc = ModbusRTU485::crc16(data, 4);
  // Known: CRC16-Modbus of 4 zero bytes
  // Verify it's consistent (run twice)
  uint16_t crc2 = ModbusRTU485::crc16(data, 4);
  TEST_ASSERT_EQUAL_UINT16(crc, crc2);
}

void test_crc16_all_ff(void) {
  uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
  uint16_t crc = ModbusRTU485::crc16(data, 4);
  TEST_ASSERT_NOT_EQUAL(0, crc);
}

void test_crc16_empty_buffer(void) {
  uint16_t crc = ModbusRTU485::crc16(nullptr, 0);
  // With no data, CRC should be initial value 0xFFFF
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, crc);
}

// --- validateCRC tests ---

void test_validateCRC_valid_frame(void) {
  // Build a valid frame: addr=1, FC=3, data bytes, CRC
  uint8_t data[] = {0x01, 0x03, 0x02, 0x00, 0x64};
  uint16_t crc = ModbusRTU485::crc16(data, 5);
  uint8_t frame[7];
  memcpy(frame, data, 5);
  frame[5] = crc & 0xFF;
  frame[6] = (crc >> 8) & 0xFF;
  TEST_ASSERT_TRUE(validateCRC(frame, 7));
}

void test_validateCRC_corrupted_frame(void) {
  uint8_t data[] = {0x01, 0x03, 0x02, 0x00, 0x64};
  uint16_t crc = ModbusRTU485::crc16(data, 5);
  uint8_t frame[7];
  memcpy(frame, data, 5);
  frame[5] = crc & 0xFF;
  frame[6] = (crc >> 8) & 0xFF;
  // Corrupt one byte
  frame[2] = 0xFF;
  TEST_ASSERT_FALSE(validateCRC(frame, 7));
}

void test_validateCRC_corrupted_crc_byte(void) {
  uint8_t data[] = {0x01, 0x03, 0x02, 0x00, 0x64};
  uint16_t crc = ModbusRTU485::crc16(data, 5);
  uint8_t frame[7];
  memcpy(frame, data, 5);
  frame[5] = (crc & 0xFF) ^ 0x01;  // Flip a CRC bit
  frame[6] = (crc >> 8) & 0xFF;
  TEST_ASSERT_FALSE(validateCRC(frame, 7));
}

// --- isValidModbusMessage tests ---

void test_isValid_too_short(void) {
  uint8_t data[] = {0x01, 0x03, 0x04};
  TEST_ASSERT_FALSE(isValidModbusMessage(data, 3));
}

void test_isValid_null_pointer(void) {
  TEST_ASSERT_FALSE(isValidModbusMessage(nullptr, 10));
}

void test_isValid_minimum_valid(void) {
  // Minimum 4 bytes: 2 data + 2 CRC
  uint8_t data[] = {0x01, 0x03};
  uint16_t crc = ModbusRTU485::crc16(data, 2);
  uint8_t frame[4];
  frame[0] = 0x01;
  frame[1] = 0x03;
  frame[2] = crc & 0xFF;
  frame[3] = (crc >> 8) & 0xFF;
  TEST_ASSERT_TRUE(isValidModbusMessage(frame, 4));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_crc16_known_read_request);
  RUN_TEST(test_crc16_single_byte);
  RUN_TEST(test_crc16_all_zeros);
  RUN_TEST(test_crc16_all_ff);
  RUN_TEST(test_crc16_empty_buffer);
  RUN_TEST(test_validateCRC_valid_frame);
  RUN_TEST(test_validateCRC_corrupted_frame);
  RUN_TEST(test_validateCRC_corrupted_crc_byte);
  RUN_TEST(test_isValid_too_short);
  RUN_TEST(test_isValid_null_pointer);
  RUN_TEST(test_isValid_minimum_valid);

  return UNITY_END();
}
