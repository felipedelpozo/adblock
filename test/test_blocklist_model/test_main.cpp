#include <unity.h>
#include <string.h>
#include "blocklist_model.h"

void test_little_endian_numeric_order() {
  const uint8_t sorted[] = {
      0xff, 0x00, 0x00, 0x00, 0x00,  // 0xff
      0x00, 0x01, 0x00, 0x00, 0x00,  // 0x100
      0x01, 0x01, 0x00, 0x00, 0x00}; // 0x101
  TEST_ASSERT_TRUE(blocklist_model::validSortedUnique(sorted, sizeof(sorted)));
  uint8_t duplicate[sizeof(sorted)];
  memcpy(duplicate, sorted, sizeof(sorted));
  memcpy(duplicate + 5, duplicate, 5);
  TEST_ASSERT_FALSE(blocklist_model::validSortedUnique(duplicate, sizeof(duplicate)));
}

void test_rejects_empty_and_partial_records() {
  const uint8_t data[] = {0, 0, 0, 0};
  TEST_ASSERT_FALSE(blocklist_model::validSortedUnique(nullptr, 0));
  TEST_ASSERT_FALSE(blocklist_model::validSortedUnique(data, sizeof(data)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_little_endian_numeric_order);
  RUN_TEST(test_rejects_empty_and_partial_records);
  return UNITY_END();
}
