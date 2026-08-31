#include <unity.h>
#include <cstring>
#include <Preferences.h>
#include "nvs_config.h"

// --- saveWiFiCredentials ---

void test_save_normal_credentials(void) {
  TEST_ASSERT_TRUE(saveWiFiCredentials("MyNetwork", "secret"));
}

// Regression: Preferences::putString() answers with the number of bytes
// written, so an empty password returns 0. Reading that as failure made
// /api/wifi reply 500 and skip its reboot for every open network.
void test_save_with_empty_password(void) {
  TEST_ASSERT_TRUE(saveWiFiCredentials("OpenNetwork", ""));
}

void test_save_with_null_password(void) {
  TEST_ASSERT_TRUE(saveWiFiCredentials("OpenNetwork", nullptr));
}

void test_save_rejects_empty_ssid(void) {
  TEST_ASSERT_FALSE(saveWiFiCredentials("", "secret"));
}

void test_save_rejects_null_ssid(void) {
  TEST_ASSERT_FALSE(saveWiFiCredentials(nullptr, "secret"));
}

// --- round trip through NVS ---

void test_saved_credentials_round_trip(void) {
  char ssid[64] = {0};
  char pass[64] = {0};

  TEST_ASSERT_TRUE(saveWiFiCredentials("MyNetwork", "secret"));
  TEST_ASSERT_TRUE(loadWiFiCredentials(ssid, sizeof(ssid), pass, sizeof(pass)));
  TEST_ASSERT_EQUAL_STRING("MyNetwork", ssid);
  TEST_ASSERT_EQUAL_STRING("secret", pass);
}

void test_empty_password_round_trip(void) {
  char ssid[64] = {0};
  char pass[64] = {0};

  TEST_ASSERT_TRUE(saveWiFiCredentials("OpenNetwork", ""));
  TEST_ASSERT_TRUE(loadWiFiCredentials(ssid, sizeof(ssid), pass, sizeof(pass)));
  TEST_ASSERT_EQUAL_STRING("OpenNetwork", ssid);
  TEST_ASSERT_EQUAL_STRING("", pass);
}

void test_load_returns_false_when_nothing_stored(void) {
  char ssid[64] = {0};
  char pass[64] = {0};
  TEST_ASSERT_FALSE(loadWiFiCredentials(ssid, sizeof(ssid), pass, sizeof(pass)));
}

// --- hasStoredWiFiCredentials / clearWiFiCredentials ---

void test_has_stored_credentials_after_save(void) {
  TEST_ASSERT_FALSE(hasStoredWiFiCredentials());
  TEST_ASSERT_TRUE(saveWiFiCredentials("MyNetwork", "secret"));
  TEST_ASSERT_TRUE(hasStoredWiFiCredentials());
}

void test_clear_removes_credentials(void) {
  TEST_ASSERT_TRUE(saveWiFiCredentials("MyNetwork", "secret"));
  TEST_ASSERT_TRUE(clearWiFiCredentials());
  TEST_ASSERT_FALSE(hasStoredWiFiCredentials());
}

// An empty SSID is what loadWiFiCredentials() treats as "nothing stored", so
// after a clear the device falls back to the network in credentials.h.
void test_load_after_clear_falls_back(void) {
  char ssid[64] = {0};
  char pass[64] = {0};

  TEST_ASSERT_TRUE(saveWiFiCredentials("MyNetwork", "secret"));
  TEST_ASSERT_TRUE(clearWiFiCredentials());
  TEST_ASSERT_FALSE(loadWiFiCredentials(ssid, sizeof(ssid), pass, sizeof(pass)));
}

void setUp(void) { Preferences::resetAll(); }
void tearDown(void) {}

int main(int argc, char** argv) {
  UNITY_BEGIN();

  RUN_TEST(test_save_normal_credentials);
  RUN_TEST(test_save_with_empty_password);
  RUN_TEST(test_save_with_null_password);
  RUN_TEST(test_save_rejects_empty_ssid);
  RUN_TEST(test_save_rejects_null_ssid);

  RUN_TEST(test_saved_credentials_round_trip);
  RUN_TEST(test_empty_password_round_trip);
  RUN_TEST(test_load_returns_false_when_nothing_stored);

  RUN_TEST(test_has_stored_credentials_after_save);
  RUN_TEST(test_clear_removes_credentials);
  RUN_TEST(test_load_after_clear_falls_back);

  return UNITY_END();
}
