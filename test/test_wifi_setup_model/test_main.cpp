#include <cstring>
#include <string>
#include <unity.h>

#include "wifi_setup_model.h"

using wifi_setup_model::Credentials;

void test_credentials_preserve_spaces_and_bounds() {
  TEST_ASSERT_TRUE(wifi_setup_model::validCredentials({" leading SSID ", " 12345678 "}));
  TEST_ASSERT_TRUE(wifi_setup_model::validPassword(""));
  TEST_ASSERT_FALSE(wifi_setup_model::validSsid(""));
  TEST_ASSERT_FALSE(wifi_setup_model::validSsid(std::string(33, 's')));
  TEST_ASSERT_FALSE(wifi_setup_model::validPassword("short"));
  TEST_ASSERT_FALSE(wifi_setup_model::validPassword(std::string(64, 'p')));
}

void test_controls_and_embedded_nul_are_rejected() {
  const char controls[] = {'a', '\n', 'b'};
  const std::string embedded("ssid\0tail", 9);
  TEST_ASSERT_FALSE(wifi_setup_model::validSsid(std::string(controls, sizeof(controls))));
  TEST_ASSERT_FALSE(wifi_setup_model::validSsid(embedded));
  TEST_ASSERT_FALSE(wifi_setup_model::validPassword("1234567\t"));
  TEST_ASSERT_FALSE(wifi_setup_model::validPassword("1234567\x7f"));
}

void test_record_round_trip_and_zero_padding() {
  const Credentials input{"Office WiFi  ", "password with spaces"};
  std::uint8_t record[wifi_setup_model::kRecordSize];
  TEST_ASSERT_EQUAL_UINT(wifi_setup_model::kRecordSize,
                         wifi_setup_model::encodeRecord(input, record, sizeof(record)));
  TEST_ASSERT_TRUE(wifi_setup_model::validRecord(record, sizeof(record)));
  Credentials decoded;
  TEST_ASSERT_TRUE(wifi_setup_model::decodeRecord(record, sizeof(record), decoded));
  TEST_ASSERT_EQUAL_STRING(input.ssid.c_str(), decoded.ssid.c_str());
  TEST_ASSERT_EQUAL_STRING(input.password.c_str(), decoded.password.c_str());
  TEST_ASSERT_EQUAL_UINT8(0, record[8 + input.ssid.size()]);
  TEST_ASSERT_EQUAL_UINT8(0, record[8 + wifi_setup_model::kMaxSsidBytes + input.password.size()]);
}

void test_record_rejects_truncation_mutation_and_invalid_lengths() {
  const Credentials input{"ssid", "12345678"};
  std::uint8_t record[wifi_setup_model::kRecordSize];
  TEST_ASSERT_EQUAL_UINT(wifi_setup_model::kRecordSize,
                         wifi_setup_model::encodeRecord(input, record, sizeof(record)));
  TEST_ASSERT_FALSE(wifi_setup_model::validRecord(record, sizeof(record) - 1));
  record[9] ^= 1;
  TEST_ASSERT_FALSE(wifi_setup_model::validRecord(record, sizeof(record)));
  TEST_ASSERT_EQUAL_UINT(0, wifi_setup_model::encodeRecord(input, record, 4));
  TEST_ASSERT_FALSE(wifi_setup_model::validRecord(nullptr, wifi_setup_model::kRecordSize));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_credentials_preserve_spaces_and_bounds);
  RUN_TEST(test_controls_and_embedded_nul_are_rejected);
  RUN_TEST(test_record_round_trip_and_zero_padding);
  RUN_TEST(test_record_rejects_truncation_mutation_and_invalid_lengths);
  return UNITY_END();
}
