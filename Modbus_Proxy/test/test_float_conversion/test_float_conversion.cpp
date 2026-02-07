#include <unity.h>
#include <cmath>
#include <cstring>
#include "dtsu666.h"

// --- parseFloat32 tests ---

void test_parseFloat32_zero(void) {
  uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_FLOAT_WITHIN(1e-10, 0.0f, parseFloat32(data, 0));
}

void test_parseFloat32_one(void) {
  // IEEE754: 1.0 = 0x3F800000
  uint8_t data[] = {0x3F, 0x80, 0x00, 0x00};
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.0f, parseFloat32(data, 0));
}

void test_parseFloat32_negative_100(void) {
  // IEEE754: -100.0 = 0xC2C80000
  uint8_t data[] = {0xC2, 0xC8, 0x00, 0x00};
  TEST_ASSERT_FLOAT_WITHIN(0.01, -100.0f, parseFloat32(data, 0));
}

void test_parseFloat32_pi(void) {
  // IEEE754: 3.14159265 ≈ 0x40490FDB
  uint8_t data[] = {0x40, 0x49, 0x0F, 0xDB};
  TEST_ASSERT_FLOAT_WITHIN(1e-5, 3.14159265f, parseFloat32(data, 0));
}

void test_parseFloat32_7400(void) {
  // IEEE754: 7400.0 = 0x45E74000
  uint8_t data[] = {0x45, 0xE7, 0x40, 0x00};
  TEST_ASSERT_FLOAT_WITHIN(0.1, 7400.0f, parseFloat32(data, 0));
}

void test_parseFloat32_nan(void) {
  // NaN: exponent all 1s, mantissa non-zero
  uint8_t data[] = {0x7F, 0xC0, 0x00, 0x00};
  TEST_ASSERT_TRUE(std::isnan(parseFloat32(data, 0)));
}

void test_parseFloat32_positive_infinity(void) {
  // +inf: 0x7F800000
  uint8_t data[] = {0x7F, 0x80, 0x00, 0x00};
  TEST_ASSERT_TRUE(std::isinf(parseFloat32(data, 0)));
  TEST_ASSERT_TRUE(parseFloat32(data, 0) > 0);
}

void test_parseFloat32_negative_infinity(void) {
  // -inf: 0xFF800000
  uint8_t data[] = {0xFF, 0x80, 0x00, 0x00};
  TEST_ASSERT_TRUE(std::isinf(parseFloat32(data, 0)));
  TEST_ASSERT_TRUE(parseFloat32(data, 0) < 0);
}

void test_parseFloat32_subnormal(void) {
  // Smallest subnormal: 0x00000001
  uint8_t data[] = {0x00, 0x00, 0x00, 0x01};
  float val = parseFloat32(data, 0);
  TEST_ASSERT_TRUE(val > 0.0f);
  TEST_ASSERT_TRUE(val < 1e-38f);
}

void test_parseFloat32_with_offset(void) {
  // Data at offset 4
  uint8_t data[] = {0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00};
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 1.0f, parseFloat32(data, 4));
}

// --- encodeFloat32 tests ---

void test_encodeFloat32_zero(void) {
  uint8_t data[4] = {};
  encodeFloat32(0.0f, data, 0);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[2]);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[3]);
}

void test_encodeFloat32_one(void) {
  uint8_t data[4] = {};
  encodeFloat32(1.0f, data, 0);
  TEST_ASSERT_EQUAL_UINT8(0x3F, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0x80, data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[2]);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[3]);
}

void test_encodeFloat32_negative(void) {
  uint8_t data[4] = {};
  encodeFloat32(-100.0f, data, 0);
  TEST_ASSERT_EQUAL_UINT8(0xC2, data[0]);
  TEST_ASSERT_EQUAL_UINT8(0xC8, data[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[2]);
  TEST_ASSERT_EQUAL_UINT8(0x00, data[3]);
}

// --- Round-trip tests ---

void test_roundtrip_float32_positive(void) {
  uint8_t data[4];
  encodeFloat32(1234.5678f, data, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.001, 1234.5678f, parseFloat32(data, 0));
}

void test_roundtrip_float32_negative(void) {
  uint8_t data[4];
  encodeFloat32(-9876.5f, data, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.1, -9876.5f, parseFloat32(data, 0));
}

void test_roundtrip_float32_small(void) {
  uint8_t data[4];
  encodeFloat32(0.001f, data, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-6, 0.001f, parseFloat32(data, 0));
}

void test_roundtrip_float32_large(void) {
  uint8_t data[4];
  encodeFloat32(22000.0f, data, 0);
  TEST_ASSERT_FLOAT_WITHIN(0.1, 22000.0f, parseFloat32(data, 0));
}

// --- parseInt16 / parseUInt16 tests ---

void test_parseInt16_zero(void) {
  uint8_t data[] = {0x00, 0x00};
  TEST_ASSERT_EQUAL_INT16(0, parseInt16(data, 0));
}

void test_parseInt16_max_positive(void) {
  uint8_t data[] = {0x7F, 0xFF};
  TEST_ASSERT_EQUAL_INT16(32767, parseInt16(data, 0));
}

void test_parseInt16_min_negative(void) {
  uint8_t data[] = {0x80, 0x00};
  TEST_ASSERT_EQUAL_INT16(-32768, parseInt16(data, 0));
}

void test_parseUInt16_zero(void) {
  uint8_t data[] = {0x00, 0x00};
  TEST_ASSERT_EQUAL_UINT16(0, parseUInt16(data, 0));
}

void test_parseUInt16_1234(void) {
  // 1234 = 0x04D2
  uint8_t data[] = {0x04, 0xD2};
  TEST_ASSERT_EQUAL_UINT16(1234, parseUInt16(data, 0));
}

void test_parseUInt16_max(void) {
  uint8_t data[] = {0xFF, 0xFF};
  TEST_ASSERT_EQUAL_UINT16(65535, parseUInt16(data, 0));
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  // parseFloat32
  RUN_TEST(test_parseFloat32_zero);
  RUN_TEST(test_parseFloat32_one);
  RUN_TEST(test_parseFloat32_negative_100);
  RUN_TEST(test_parseFloat32_pi);
  RUN_TEST(test_parseFloat32_7400);
  RUN_TEST(test_parseFloat32_nan);
  RUN_TEST(test_parseFloat32_positive_infinity);
  RUN_TEST(test_parseFloat32_negative_infinity);
  RUN_TEST(test_parseFloat32_subnormal);
  RUN_TEST(test_parseFloat32_with_offset);

  // encodeFloat32
  RUN_TEST(test_encodeFloat32_zero);
  RUN_TEST(test_encodeFloat32_one);
  RUN_TEST(test_encodeFloat32_negative);

  // Round-trips
  RUN_TEST(test_roundtrip_float32_positive);
  RUN_TEST(test_roundtrip_float32_negative);
  RUN_TEST(test_roundtrip_float32_small);
  RUN_TEST(test_roundtrip_float32_large);

  // Int16 / UInt16
  RUN_TEST(test_parseInt16_zero);
  RUN_TEST(test_parseInt16_max_positive);
  RUN_TEST(test_parseInt16_min_negative);
  RUN_TEST(test_parseUInt16_zero);
  RUN_TEST(test_parseUInt16_1234);
  RUN_TEST(test_parseUInt16_max);

  return UNITY_END();
}
