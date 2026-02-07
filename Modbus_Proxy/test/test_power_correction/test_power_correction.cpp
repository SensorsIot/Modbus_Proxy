#include <unity.h>
#include <cmath>
#include <cstring>
#include "dtsu666.h"

// Local reimplementation of shouldApplyCorrection (avoids compiling modbus_proxy.cpp)
static bool shouldApplyCorrection(float power) {
  return fabs(power) > 1000.0f;
}

// Helper: build a minimal valid 165-byte DTSU response buffer with all zeros for power fields
static void buildZeroResponse(uint8_t* buf) {
  memset(buf, 0, 165);
  buf[0] = 0x0B;  // Slave address
  buf[1] = 0x03;  // Function code
  buf[2] = 0xA0;  // 160 bytes payload

  // Calculate and set CRC
  uint16_t crc = ModbusRTU485::crc16(buf, 163);
  buf[163] = crc & 0xFF;
  buf[164] = (crc >> 8) & 0xFF;
}

// Helper: read float at payload offset from raw buffer
static float readFloat(const uint8_t* raw, size_t payloadOffset) {
  return parseFloat32(raw + 3, payloadOffset);
}

// --- shouldApplyCorrection tests ---

void test_correction_above_threshold(void) {
  TEST_ASSERT_TRUE(shouldApplyCorrection(1500.0f));
}

void test_correction_below_threshold(void) {
  TEST_ASSERT_FALSE(shouldApplyCorrection(500.0f));
}

void test_correction_at_threshold(void) {
  // Exactly 1000W should NOT trigger (> not >=)
  TEST_ASSERT_FALSE(shouldApplyCorrection(1000.0f));
}

void test_correction_zero(void) {
  TEST_ASSERT_FALSE(shouldApplyCorrection(0.0f));
}

void test_correction_negative_above_threshold(void) {
  // Negative but |value| > 1000
  TEST_ASSERT_TRUE(shouldApplyCorrection(-1500.0f));
}

// --- applyPowerCorrection buffer validation ---

void test_correction_null_buffer(void) {
  TEST_ASSERT_FALSE(applyPowerCorrection(nullptr, 165, 1000.0f));
}

void test_correction_short_buffer(void) {
  uint8_t buf[100];
  memset(buf, 0, 100);
  TEST_ASSERT_FALSE(applyPowerCorrection(buf, 100, 1000.0f));
}

void test_correction_valid_buffer_returns_true(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);
  TEST_ASSERT_TRUE(applyPowerCorrection(buf, 165, 1000.0f));
}

// --- Power correction math ---

void test_correction_total_power(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  float correction = 3000.0f;
  applyPowerCorrection(buf, 165, correction);

  // Total power is at payload offset 12*4 = 48
  float totalPower = readFloat(buf, 48);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 3000.0f, totalPower);
}

void test_correction_phase_power_distribution(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  float correction = 3000.0f;
  applyPowerCorrection(buf, 165, correction);

  // Phase powers at offsets 13*4=52, 14*4=56, 15*4=60
  float perPhase = correction / 3.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.1f, perPhase, readFloat(buf, 52));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, perPhase, readFloat(buf, 56));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, perPhase, readFloat(buf, 60));
}

void test_correction_demand_fields(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  float correction = 6000.0f;
  applyPowerCorrection(buf, 165, correction);

  // Demand total at payload offset 28*4 = 112
  float demandTotal = readFloat(buf, 112);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 6000.0f, demandTotal);

  // Demand per-phase at 29*4=116, 30*4=120, 31*4=124
  float perPhase = correction / 3.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.1f, perPhase, readFloat(buf, 116));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, perPhase, readFloat(buf, 120));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, perPhase, readFloat(buf, 124));
}

void test_correction_large_22kw(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  applyPowerCorrection(buf, 165, 22000.0f);
  float totalPower = readFloat(buf, 48);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 22000.0f, totalPower);
}

void test_correction_negative(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  applyPowerCorrection(buf, 165, -5000.0f);
  float totalPower = readFloat(buf, 48);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -5000.0f, totalPower);
}

void test_correction_zero_correction(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  applyPowerCorrection(buf, 165, 0.0f);
  float totalPower = readFloat(buf, 48);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, totalPower);
}

void test_correction_preserves_non_power_fields(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  // Set voltage_L1N at payload offset 4*4=16 to 230.0V
  encodeFloat32(230.0f, buf + 3, 16);
  // Set frequency at payload offset 11*4=44 to 50.0Hz
  encodeFloat32(50.0f, buf + 3, 44);

  // Recalculate CRC before correction
  uint16_t crc = ModbusRTU485::crc16(buf, 163);
  buf[163] = crc & 0xFF;
  buf[164] = (crc >> 8) & 0xFF;

  applyPowerCorrection(buf, 165, 3000.0f);

  // Non-power fields should be unchanged
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 230.0f, readFloat(buf, 16));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, readFloat(buf, 44));
}

void test_correction_crc_recalculated(void) {
  uint8_t buf[165];
  buildZeroResponse(buf);

  applyPowerCorrection(buf, 165, 5000.0f);

  // Verify CRC is valid after correction
  uint16_t calc = ModbusRTU485::crc16(buf, 163);
  uint16_t stored = (uint16_t)buf[163] | ((uint16_t)buf[164] << 8);
  TEST_ASSERT_EQUAL_UINT16(calc, stored);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  // Threshold tests
  RUN_TEST(test_correction_above_threshold);
  RUN_TEST(test_correction_below_threshold);
  RUN_TEST(test_correction_at_threshold);
  RUN_TEST(test_correction_zero);
  RUN_TEST(test_correction_negative_above_threshold);

  // Buffer validation
  RUN_TEST(test_correction_null_buffer);
  RUN_TEST(test_correction_short_buffer);
  RUN_TEST(test_correction_valid_buffer_returns_true);

  // Power math
  RUN_TEST(test_correction_total_power);
  RUN_TEST(test_correction_phase_power_distribution);
  RUN_TEST(test_correction_demand_fields);
  RUN_TEST(test_correction_large_22kw);
  RUN_TEST(test_correction_negative);
  RUN_TEST(test_correction_zero_correction);
  RUN_TEST(test_correction_preserves_non_power_fields);
  RUN_TEST(test_correction_crc_recalculated);

  return UNITY_END();
}
