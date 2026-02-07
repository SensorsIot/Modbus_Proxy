#include <unity.h>
#include <cstring>
#include "nvs_config.h"
#include "config.h"

// --- getDefaultConfig tests ---

void test_default_host(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  TEST_ASSERT_EQUAL_STRING("192.168.0.203", config.host);
}

void test_default_port(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  TEST_ASSERT_EQUAL_UINT16(1883, config.port);
}

void test_default_user(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  TEST_ASSERT_EQUAL_STRING("admin", config.user);
}

void test_default_pass(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  TEST_ASSERT_EQUAL_STRING("admin", config.pass);
}

void test_default_wallbox_topic(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  TEST_ASSERT_EQUAL_STRING("wallbox", config.wallboxTopic);
}

void test_default_log_level(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  TEST_ASSERT_EQUAL_UINT8(LOG_LEVEL_WARN, config.logLevel);
}

void test_host_null_terminated(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  // Last byte of host array should be null terminator
  TEST_ASSERT_EQUAL_UINT8('\0', config.host[sizeof(config.host) - 1]);
}

void test_user_null_terminated(void) {
  MQTTConfig config;
  getDefaultConfig(config);
  TEST_ASSERT_EQUAL_UINT8('\0', config.user[sizeof(config.user) - 1]);
}

// --- Constants tests ---

void test_correction_threshold(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, CORRECTION_THRESHOLD);
}

void test_wallbox_max_age(void) {
  TEST_ASSERT_EQUAL_UINT32(30000, WALLBOX_DATA_MAX_AGE_MS);
}

void test_watchdog_timeout(void) {
  TEST_ASSERT_EQUAL_UINT32(60000, WATCHDOG_TIMEOUT_MS);
}

void test_min_free_heap(void) {
  TEST_ASSERT_EQUAL_UINT32(20000, MIN_FREE_HEAP);
}

void setUp(void) {}
void tearDown(void) {}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_default_host);
  RUN_TEST(test_default_port);
  RUN_TEST(test_default_user);
  RUN_TEST(test_default_pass);
  RUN_TEST(test_default_wallbox_topic);
  RUN_TEST(test_default_log_level);
  RUN_TEST(test_host_null_terminated);
  RUN_TEST(test_user_null_terminated);
  RUN_TEST(test_correction_threshold);
  RUN_TEST(test_wallbox_max_age);
  RUN_TEST(test_watchdog_timeout);
  RUN_TEST(test_min_free_heap);

  return UNITY_END();
}
