#include <unity.h>
#include <cmath>
#include <cstring>
#include "dtsu666.h"

// Helper: build a valid ModbusMessage with a 165-byte DTSU response
static void buildValidMessage(uint8_t* rawBuf, ModbusMessage& msg) {
  memset(rawBuf, 0, 165);
  rawBuf[0] = 0x0B;  // Slave ID
  rawBuf[1] = 0x03;  // FC read holding
  rawBuf[2] = 0xA0;  // 160 bytes payload

  // Set some test values in the payload (offset from rawBuf+3)
  // current_L1 at offset 0 = 10.5A
  encodeFloat32(10.5f, rawBuf + 3, 0);
  // voltage_L1N at offset 4*4=16 = 230.0V
  encodeFloat32(230.0f, rawBuf + 3, 16);
  // power_total at offset 12*4=48 = -5000.0W (inverted in meter)
  encodeFloat32(-5000.0f, rawBuf + 3, 48);
  // frequency at offset 11*4=44 = 50.0Hz
  encodeFloat32(50.0f, rawBuf + 3, 44);

  // CRC
  uint16_t crc = ModbusRTU485::crc16(rawBuf, 163);
  rawBuf[163] = crc & 0xFF;
  rawBuf[164] = (crc >> 8) & 0xFF;

  msg.valid = true;
  msg.type = MBType::Reply;
  msg.fc = 0x03;
  msg.id = 0x0B;
  msg.raw = rawBuf;
  msg.len = 165;
  msg.byteCount = 160;
}

// --- parseDTSU666Data tests ---

void test_parse_valid_data(void) {
  uint8_t rawBuf[165];
  ModbusMessage msg;
  buildValidMessage(rawBuf, msg);

  DTSU666Data data = {};
  bool result = parseDTSU666Data(2102, msg, data);
  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 10.5f, data.current_L1);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 230.0f, data.voltage_L1N);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, data.frequency);
}

void test_parse_invalid_message(void) {
  ModbusMessage msg = {};
  msg.valid = false;
  msg.type = MBType::Reply;
  msg.raw = nullptr;

  DTSU666Data data = {};
  TEST_ASSERT_FALSE(parseDTSU666Data(2102, msg, data));
}

void test_parse_wrong_type(void) {
  uint8_t rawBuf[165];
  ModbusMessage msg;
  buildValidMessage(rawBuf, msg);
  msg.type = MBType::Request;  // Wrong type

  DTSU666Data data = {};
  TEST_ASSERT_FALSE(parseDTSU666Data(2102, msg, data));
}

void test_parse_null_raw(void) {
  ModbusMessage msg = {};
  msg.valid = true;
  msg.type = MBType::Reply;
  msg.raw = nullptr;

  DTSU666Data data = {};
  TEST_ASSERT_FALSE(parseDTSU666Data(2102, msg, data));
}

void test_parse_wrong_payload_size(void) {
  uint8_t rawBuf[165];
  ModbusMessage msg;
  buildValidMessage(rawBuf, msg);
  rawBuf[2] = 0x50;  // Wrong payload size (80 instead of 160)

  DTSU666Data data = {};
  TEST_ASSERT_FALSE(parseDTSU666Data(2102, msg, data));
}

void test_parse_power_sign_inversion(void) {
  uint8_t rawBuf[165];
  ModbusMessage msg;
  buildValidMessage(rawBuf, msg);

  // Set raw power_total to 3000.0 (positive in wire format)
  encodeFloat32(3000.0f, rawBuf + 3, 48);
  // Recalculate CRC
  uint16_t crc = ModbusRTU485::crc16(rawBuf, 163);
  rawBuf[163] = crc & 0xFF;
  rawBuf[164] = (crc >> 8) & 0xFF;

  DTSU666Data data = {};
  parseDTSU666Data(2102, msg, data);

  // power_scale = -1.0f, so parsed value should be negative
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -3000.0f, data.power_total);
}

void test_parse_voltage_no_inversion(void) {
  uint8_t rawBuf[165];
  ModbusMessage msg;
  buildValidMessage(rawBuf, msg);

  // Voltage should pass through unchanged (volt_scale = 1.0f)
  DTSU666Data data = {};
  parseDTSU666Data(2102, msg, data);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 230.0f, data.voltage_L1N);
}

// --- parseDTSU666Response tests ---

void test_parseResponse_valid(void) {
  uint8_t rawBuf[165];
  memset(rawBuf, 0, 165);
  rawBuf[0] = 0x0B;
  rawBuf[1] = 0x03;
  rawBuf[2] = 0xA0;

  // Set power_total at payload offset 48
  encodeFloat32(1234.0f, rawBuf + 3, 48);

  DTSU666Data data = {};
  bool result = parseDTSU666Response(rawBuf, 165, data);
  TEST_ASSERT_TRUE(result);
  // parseDTSU666Response reads raw values without sign inversion
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 1234.0f, data.power_total);
}

void test_parseResponse_null(void) {
  DTSU666Data data = {};
  TEST_ASSERT_FALSE(parseDTSU666Response(nullptr, 165, data));
}

void test_parseResponse_short(void) {
  uint8_t rawBuf[100];
  memset(rawBuf, 0, 100);
  DTSU666Data data = {};
  TEST_ASSERT_FALSE(parseDTSU666Response(rawBuf, 100, data));
}

// --- encodeDTSU666Response tests ---

void test_encode_basic(void) {
  DTSU666Data data = {};
  data.current_L1 = 10.0f;
  data.voltage_L1N = 230.0f;
  data.frequency = 50.0f;

  uint8_t buf[165];
  bool result = encodeDTSU666Response(data, buf, 165);
  TEST_ASSERT_TRUE(result);
  TEST_ASSERT_EQUAL_UINT8(0x0B, buf[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03, buf[1]);
  TEST_ASSERT_EQUAL_UINT8(0xA0, buf[2]);
}

void test_encode_buffer_too_small(void) {
  DTSU666Data data = {};
  uint8_t buf[100];
  TEST_ASSERT_FALSE(encodeDTSU666Response(data, buf, 100));
}

void test_encode_crc_valid(void) {
  DTSU666Data data = {};
  data.power_total = 5000.0f;

  uint8_t buf[165];
  encodeDTSU666Response(data, buf, 165);

  uint16_t calc = ModbusRTU485::crc16(buf, 163);
  uint16_t stored = (uint16_t)buf[163] | ((uint16_t)buf[164] << 8);
  TEST_ASSERT_EQUAL_UINT16(calc, stored);
}

// --- Encode-parse round-trip ---

void test_encode_parse_roundtrip(void) {
  DTSU666Data original = {};
  original.current_L1 = 15.3f;
  original.current_L2 = 12.1f;
  original.current_L3 = 14.7f;
  original.voltage_LN_avg = 230.5f;
  original.voltage_L1N = 231.0f;
  original.voltage_L2N = 229.8f;
  original.voltage_L3N = 230.7f;
  original.voltage_LL_avg = 400.0f;
  original.voltage_L1L2 = 399.5f;
  original.voltage_L2L3 = 400.2f;
  original.voltage_L3L1 = 400.3f;
  original.frequency = 50.01f;
  original.power_total = 5000.0f;
  original.power_L1 = 1700.0f;
  original.power_L2 = 1650.0f;
  original.power_L3 = 1650.0f;
  original.reactive_total = 100.0f;
  original.reactive_L1 = 33.0f;
  original.reactive_L2 = 34.0f;
  original.reactive_L3 = 33.0f;
  original.apparent_total = 5001.0f;
  original.apparent_L1 = 1700.5f;
  original.apparent_L2 = 1650.5f;
  original.apparent_L3 = 1650.0f;
  original.pf_total = 0.99f;
  original.pf_L1 = 0.98f;
  original.pf_L2 = 0.99f;
  original.pf_L3 = 1.00f;
  original.demand_total = 4800.0f;
  original.demand_L1 = 1600.0f;
  original.demand_L2 = 1600.0f;
  original.demand_L3 = 1600.0f;
  original.import_total = 12345.6f;
  original.import_L1 = 4115.2f;
  original.import_L2 = 4115.2f;
  original.import_L3 = 4115.2f;
  original.export_total = 6789.0f;
  original.export_L1 = 2263.0f;
  original.export_L2 = 2263.0f;
  original.export_L3 = 2263.0f;

  uint8_t buf[165];
  TEST_ASSERT_TRUE(encodeDTSU666Response(original, buf, 165));

  // parseDTSU666Response reads without sign inversion
  // encodeDTSU666Response applies power_scale=-1 to power/demand fields
  // So power values will be negated in wire format
  DTSU666Data parsed = {};
  TEST_ASSERT_TRUE(parseDTSU666Response(buf, 165, parsed));

  // Non-power fields should round-trip exactly
  TEST_ASSERT_FLOAT_WITHIN(0.1f, original.current_L1, parsed.current_L1);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, original.voltage_L1N, parsed.voltage_L1N);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, original.frequency, parsed.frequency);

  // Power fields are negated by encode (wire_power = data.power * -1)
  // parseDTSU666Response reads wire values directly
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -original.power_total, parsed.power_total);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -original.power_L1, parsed.power_L1);
}

void test_encode_parse_all_zero(void) {
  DTSU666Data original = {};
  memset(&original, 0, sizeof(original));

  uint8_t buf[165];
  TEST_ASSERT_TRUE(encodeDTSU666Response(original, buf, 165));

  DTSU666Data parsed = {};
  TEST_ASSERT_TRUE(parseDTSU666Response(buf, 165, parsed));

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, parsed.current_L1);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, parsed.voltage_L1N);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, parsed.power_total);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  // parseDTSU666Data
  RUN_TEST(test_parse_valid_data);
  RUN_TEST(test_parse_invalid_message);
  RUN_TEST(test_parse_wrong_type);
  RUN_TEST(test_parse_null_raw);
  RUN_TEST(test_parse_wrong_payload_size);
  RUN_TEST(test_parse_power_sign_inversion);
  RUN_TEST(test_parse_voltage_no_inversion);

  // parseDTSU666Response
  RUN_TEST(test_parseResponse_valid);
  RUN_TEST(test_parseResponse_null);
  RUN_TEST(test_parseResponse_short);

  // encodeDTSU666Response
  RUN_TEST(test_encode_basic);
  RUN_TEST(test_encode_buffer_too_small);
  RUN_TEST(test_encode_crc_valid);

  // Round-trip
  RUN_TEST(test_encode_parse_roundtrip);
  RUN_TEST(test_encode_parse_all_zero);

  return UNITY_END();
}
