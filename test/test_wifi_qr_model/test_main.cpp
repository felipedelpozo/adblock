#include <cstring>

#include <unity.h>

#include "wifi_qr.h"

void setUp() {}
void tearDown() {}

void test_setup_payload_is_gated_to_portal_and_uses_ap_only() {
  char ap[round_ui::kSetupWifiApCapacity] = "C3-AdBlock-AB12";
  char output[round_ui::kSetupWifiPayloadCapacity];
  std::memset(output, 'x', sizeof(output));
  TEST_ASSERT_FALSE(round_ui::setupWifiPayload(false, ap, output, sizeof(output)));
  TEST_ASSERT_EQUAL_CHAR('\0', output[0]);
  TEST_ASSERT_TRUE(round_ui::setupWifiPayload(true, ap, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("WIFI:T:nopass;S:C3-AdBlock-AB12;;", output);
  TEST_ASSERT_NOT_NULL(std::strstr(output, "nopass"));
}

void test_setup_payload_escapes_wifi_delimiters() {
  char ap[round_ui::kSetupWifiApCapacity] = "A;B,C:D\\E\"F";
  char output[round_ui::kSetupWifiPayloadCapacity] = {};
  TEST_ASSERT_TRUE(round_ui::setupWifiPayload(true, ap, output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("WIFI:T:nopass;S:A\\;B\\,C\\:D\\\\E\\\"F;;", output);
}

void test_setup_payload_rejects_controls_missing_terminator_and_overflow() {
  char output[round_ui::kSetupWifiPayloadCapacity];
  char control[round_ui::kSetupWifiApCapacity] = {'A', '\n', 'B', '\0'};
  TEST_ASSERT_FALSE(round_ui::setupWifiPayload(true, control, output, sizeof(output)));

  char unterminated[round_ui::kSetupWifiApCapacity];
  std::memset(unterminated, 'A', sizeof(unterminated));
  TEST_ASSERT_FALSE(round_ui::setupWifiPayload(true, unterminated, output, sizeof(output)));

  char escapedTooLong[round_ui::kSetupWifiApCapacity];
  std::memset(escapedTooLong, '\\', round_ui::kSetupWifiApMaxBytes);
  escapedTooLong[round_ui::kSetupWifiApMaxBytes] = '\0';
  TEST_ASSERT_FALSE(round_ui::setupWifiPayload(true, escapedTooLong, output, sizeof(output)));
  TEST_ASSERT_EQUAL_CHAR('\0', output[0]);
  TEST_ASSERT_FALSE(round_ui::setupWifiPayload(true, "AP", output, 5));
  TEST_ASSERT_EQUAL_CHAR('\0', output[0]);
}

void test_setup_payload_accepts_bounded_maximum_when_unescaped() {
  char ap[round_ui::kSetupWifiApCapacity];
  std::memset(ap, 'A', round_ui::kSetupWifiApMaxBytes);
  ap[round_ui::kSetupWifiApMaxBytes] = '\0';
  char output[round_ui::kSetupWifiPayloadCapacity] = {};
  TEST_ASSERT_TRUE(round_ui::setupWifiPayload(true, ap, output, sizeof(output)));
  TEST_ASSERT_EQUAL_UINT(41, std::strlen(output));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_setup_payload_is_gated_to_portal_and_uses_ap_only);
  RUN_TEST(test_setup_payload_escapes_wifi_delimiters);
  RUN_TEST(test_setup_payload_rejects_controls_missing_terminator_and_overflow);
  RUN_TEST(test_setup_payload_accepts_bounded_maximum_when_unescaped);
  return UNITY_END();
}
