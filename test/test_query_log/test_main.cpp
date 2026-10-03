#include <unity.h>
#include "query_log.h"

void test_empty_and_newest_order() {
  blocked_log::Ring ring;
  TEST_ASSERT_EQUAL_UINT(0, ring.size());
  TEST_ASSERT_NULL(ring.newest(0));
  ring.record("ads.example", 123, 1, blocked_log::Reason::Custom, 42);
  ring.record("tracker.example", 456, 28, blocked_log::Reason::Blocklist, 43);
  TEST_ASSERT_EQUAL_STRING("tracker.example", ring.newest(0)->domain);
  TEST_ASSERT_EQUAL_UINT32(2, ring.newest(0)->id);
  TEST_ASSERT_EQUAL_UINT32(456, ring.newest(0)->client);
  TEST_ASSERT_EQUAL_UINT16(28, ring.newest(0)->type);
  TEST_ASSERT_EQUAL_UINT32(43, ring.newest(0)->uptimeSeconds);
  TEST_ASSERT_EQUAL_STRING("ads.example", ring.newest(1)->domain);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(blocked_log::Reason::Custom), static_cast<int>(ring.newest(1)->reason));
  TEST_ASSERT_NULL(ring.newest(2));
}

void test_capacity_and_bounded_copy() {
  blocked_log::Ring ring;
  char longDomain[400];
  memset(longDomain, 'a', sizeof(longDomain)); longDomain[399] = '\0';
  for (size_t i = 0; i < 1000; ++i) ring.record(longDomain, i, 1, blocked_log::Reason::Client, i);
  TEST_ASSERT_EQUAL_UINT(64, ring.size());
  TEST_ASSERT_EQUAL_UINT32(1000, ring.newest(0)->id);
  TEST_ASSERT_EQUAL_UINT32(937, ring.newest(63)->id);
  TEST_ASSERT_EQUAL_UINT(255, strlen(ring.newest(0)->domain));
  TEST_ASSERT_NULL(ring.newest(64));
}

void test_case_insensitive_search() {
  TEST_ASSERT_TRUE(blocked_log::containsIgnoreCase("Ads.Example.net", "example"));
  TEST_ASSERT_TRUE(blocked_log::containsIgnoreCase("ads.example.net", "ADS"));
  TEST_ASSERT_TRUE(blocked_log::containsIgnoreCase("192.168.31.1", "31.1"));
  TEST_ASSERT_TRUE(blocked_log::containsIgnoreCase("", ""));
  TEST_ASSERT_FALSE(blocked_log::containsIgnoreCase("ads", "ads.example"));
  TEST_ASSERT_FALSE(blocked_log::containsIgnoreCase("ads.example", "elsewhere"));
}

void test_json_control_bytes_and_quotes() {
  char escaped[7];
  TEST_ASSERT_EQUAL_UINT(2, blocked_log::escapeJsonByte('"', escaped));
  TEST_ASSERT_EQUAL_STRING("\\\"", escaped);
  blocked_log::escapeJsonByte('\\', escaped); TEST_ASSERT_EQUAL_STRING("\\\\", escaped);
  blocked_log::escapeJsonByte('\n', escaped); TEST_ASSERT_EQUAL_STRING("\\u000a", escaped);
  blocked_log::escapeJsonByte(0xff, escaped); TEST_ASSERT_EQUAL_STRING("\\u00ff", escaped);
  blocked_log::escapeJsonByte('x', escaped); TEST_ASSERT_EQUAL_STRING("x", escaped);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_and_newest_order);
  RUN_TEST(test_capacity_and_bounded_copy);
  RUN_TEST(test_case_insensitive_search);
  RUN_TEST(test_json_control_bytes_and_quotes);
  return UNITY_END();
}
