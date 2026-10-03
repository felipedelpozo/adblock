#include <unity.h>
#include "blocking_state.h"

void test_touch_durations_and_resume() {
  BlockingState state;
  TEST_ASSERT_TRUE(state.active());
  state.pause(42, 300);
  TEST_ASSERT_FALSE(state.active());
  TEST_ASSERT_EQUAL_UINT32(300, state.remainingSeconds(42));
  state.tick(300041);
  TEST_ASSERT_FALSE(state.active());
  state.tick(300042);
  TEST_ASSERT_TRUE(state.active());
  state.pause(400000, 1800);
  TEST_ASSERT_EQUAL_UINT32(1800, state.remainingSeconds(400000));
  state.resume();
  TEST_ASSERT_TRUE(state.active());
}
void test_rollover_zero_deadline_and_no_underflow() {
  BlockingState state;
  state.pause(UINT32_MAX - 999U, 1);
  TEST_ASSERT_EQUAL_UINT32(1, state.remainingSeconds(UINT32_MAX));
  state.tick(0);
  TEST_ASSERT_TRUE(state.active());
  state.pause(UINT32_MAX - 10U, 300);
  TEST_ASSERT_EQUAL_UINT32(300, state.remainingSeconds(20));
  TEST_ASSERT_EQUAL_UINT32(0, state.remainingSeconds(300000));
  state.tick(300000);
  TEST_ASSERT_TRUE(state.active());
}
void test_indefinite_and_large_input() {
  BlockingState state;
  state.pause(0, 0);
  state.tick(UINT32_MAX);
  TEST_ASSERT_FALSE(state.active());
  TEST_ASSERT_EQUAL_UINT32(0, state.remainingSeconds(UINT32_MAX));
  state.pause(10, UINT32_MAX);
  TEST_ASSERT_EQUAL_UINT32(2147483, state.remainingSeconds(10));
  state.tick(2147483010U);
  TEST_ASSERT_TRUE(state.active());
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_touch_durations_and_resume);
  RUN_TEST(test_rollover_zero_deadline_and_no_underflow);
  RUN_TEST(test_indefinite_and_large_input);
  return UNITY_END();
}
