#include <climits>
#include <unity.h>

#include "ui_model.h"

void test_hit_testing() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause5Minutes),
                        static_cast<int>(round_ui::hitTest(45, 180)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause30Minutes),
                        static_cast<int>(round_ui::hitTest(100, 180)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Resume),
                        static_cast<int>(round_ui::hitTest(160, 180)));
  // The visible button rectangles are half-open and share a deliberate gap.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause5Minutes),
                        static_cast<int>(round_ui::hitTest(38, 174)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause5Minutes),
                        static_cast<int>(round_ui::hitTest(87, 205)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(round_ui::hitTest(88, 180)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(round_ui::hitTest(95, 206)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(round_ui::hitTest(20, 180)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(round_ui::hitTest(20, 173)));
}

void test_percentage_widens_counters() {
  TEST_ASSERT_EQUAL_UINT8(0, round_ui::blockedPercent(0, 0));
  TEST_ASSERT_EQUAL_UINT8(50, round_ui::blockedPercent(UINT32_MAX, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT8(100, round_ui::blockedPercent(UINT32_MAX, 0));
  TEST_ASSERT_EQUAL_UINT8(25, round_ui::blockedPercent(1, 3));
}

void test_refresh_wraparound() {
  const uint32_t last = UINT32_MAX - 10U;
  TEST_ASSERT_TRUE(round_ui::refreshDue(5U, last, 16U));
  TEST_ASSERT_FALSE(round_ui::refreshDue(4U, last, 16U));
  TEST_ASSERT_TRUE(round_ui::refreshDue(200U, 0U, 200U));
}

void test_controls_repaint_on_pause_resume_and_portal() {
  round_ui::Snapshot active;
  round_ui::Snapshot paused = active; paused.blocking = false;
  TEST_ASSERT_TRUE(round_ui::controlStateChanged(active, paused));
  TEST_ASSERT_TRUE(round_ui::controlStateChanged(paused, active));
  round_ui::Snapshot portal = active; portal.portal = true;
  TEST_ASSERT_TRUE(round_ui::controlStateChanged(active, portal));
  TEST_ASSERT_TRUE(round_ui::controlStateChanged(portal, active));
  round_ui::Snapshot counted = active; counted.blocked = 500; counted.rssi = -60;
  TEST_ASSERT_FALSE(round_ui::controlStateChanged(active, counted));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hit_testing);
  RUN_TEST(test_percentage_widens_counters);
  RUN_TEST(test_refresh_wraparound);
  RUN_TEST(test_controls_repaint_on_pause_resume_and_portal);
  return UNITY_END();
}
