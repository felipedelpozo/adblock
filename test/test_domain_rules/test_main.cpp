#include <unity.h>
#include "domain_rules.h"

void test_normalization_and_strict_rejection() {
  char value[domain_rules::kMaxDomainLength + 1];
  TEST_ASSERT_TRUE(domain_rules::normalize("  Ads.Example.COM. ", value, sizeof(value)));
  TEST_ASSERT_EQUAL_STRING("ads.example.com", value);
  const char* invalid[] = {"||ads.example.com", "ads.example.com^", "*.example.com",
                           "ads.example.com/path", "ads..example.com", "-ads.example.com",
                           "ads-.example.com", "ads_example.com", "localhost", ""};
  for (const char* candidate : invalid) {
    TEST_ASSERT_FALSE(domain_rules::normalize(candidate, value, sizeof(value)));
  }
}

void test_suffix_allowlist_and_sorted_unique_table() {
  domain_rules::Table table;
  TEST_ASSERT_TRUE(table.add("Example.COM"));
  TEST_ASSERT_TRUE(table.add("ads.other.test"));
  TEST_ASSERT_FALSE(table.add("example.com"));
  TEST_ASSERT_TRUE(table.containsSuffix("cdn.example.com"));
  TEST_ASSERT_TRUE(table.containsSuffix("ads.other.test."));
  TEST_ASSERT_FALSE(table.containsSuffix("example.net"));
  TEST_ASSERT_FALSE(table.containsSuffix("badexample.com"));
  TEST_ASSERT_EQUAL_STRING("ads.other.test", table.at(0));
  TEST_ASSERT_EQUAL_STRING("example.com", table.at(1));
  TEST_ASSERT_TRUE(table.remove("EXAMPLE.COM."));
  TEST_ASSERT_FALSE(table.containsSuffix("cdn.example.com"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_normalization_and_strict_rejection);
  RUN_TEST(test_suffix_allowlist_and_sorted_unique_table);
  return UNITY_END();
}
