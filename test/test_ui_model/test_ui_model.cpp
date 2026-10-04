#include <climits>
#include <cstring>
#include <unity.h>

#include "ui_model.h"
#include "touch_model.h"

void test_hit_testing() {
  using namespace round_ui;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause5Minutes),
                        static_cast<int>(hitTest(Page::Controls, true, false, 120, 100)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause30Minutes),
                        static_cast<int>(hitTest(Page::Controls, true, false, 120, 150)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Resume),
                        static_cast<int>(hitTest(Page::Controls, false, false, 120, 180)));
  // The visible button rectangles are half-open and share a deliberate gap.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause5Minutes),
                        static_cast<int>(hitTest(Page::Controls, true, false, 50, 91)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::Pause5Minutes),
                        static_cast<int>(hitTest(Page::Controls, true, false, 189, 124)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(hitTest(Page::Controls, true, false, 190, 100)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(hitTest(Page::Controls, true, false, 120, 125)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(hitTest(Page::Controls, true, false, 20, 100)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(round_ui::Action::None),
                        static_cast<int>(hitTest(Page::Controls, true, false, 120, 90)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(hitTest(Page::Status, false, false, 120, 100)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(hitTest(Page::Controls, false, true, 120, 100)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(hitTest(Page::Controls, true, false, 120, 180)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(hitTest(Page::Controls, false, false, 120, 200)));
}

void test_percentage_widens_counters() {
  TEST_ASSERT_EQUAL_UINT8(0, round_ui::blockedPercent(0, 0));
  TEST_ASSERT_EQUAL_UINT8(50, round_ui::blockedPercent(UINT32_MAX, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT8(100, round_ui::blockedPercent(UINT32_MAX, 0));
  TEST_ASSERT_EQUAL_UINT8(25, round_ui::blockedPercent(1, 3));
  TEST_ASSERT_EQUAL_UINT16(0, round_ui::blockedPercentTenths(0, 0));
  TEST_ASSERT_EQUAL_UINT16(500, round_ui::blockedPercentTenths(UINT32_MAX, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT16(1000, round_ui::blockedPercentTenths(UINT32_MAX, 0));
  TEST_ASSERT_EQUAL_UINT16(248, round_ui::blockedPercentTenths(1248, 3784));
}

void test_cyclic_pages() {
  using namespace round_ui;
  const Page expected[] = {Page::PetHome, Page::Status, Page::Activity, Page::Lists,
                           Page::Network, Page::Controls, Page::PetStatus};
  Page page = Page::PetHome;
  for (uint8_t i = 1; i <= kPageCount; ++i) {
    page = adjacentPage(page, 1);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(expected[i % kPageCount]), static_cast<int>(page));
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::PetStatus),
                        static_cast<int>(adjacentPage(Page::PetHome, -1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Network), static_cast<int>(adjacentPage(Page::Controls, -1)));
}

void test_pet_home_and_status_are_presentation_only() {
  using namespace round_ui;
  Snapshot snapshot;
  Navigation navigation;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::PetHome), static_cast<int>(navigation.page));

  Gesture tap;
  tap.kind = GestureKind::Tap;
  tap.startX = tap.x = 120;
  tap.startY = tap.y = 120;
  TEST_ASSERT_TRUE(petTap(tap));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(navigation.handle(tap, snapshot, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::PetHome), static_cast<int>(navigation.page));

  tap.startY = tap.y = 54;
  TEST_ASSERT_TRUE(petTap(tap));
  tap.startY = tap.y = 53;
  TEST_ASSERT_FALSE(petTap(tap));
  tap.startY = tap.y = 120;
  tap.startX = tap.x = 20;
  TEST_ASSERT_FALSE(petTap(tap));

  Gesture swipe;
  swipe.kind = GestureKind::SwipeRight;
  navigation.handle(swipe, snapshot, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::PetStatus), static_cast<int>(navigation.page));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(navigation.handle(tap, snapshot, true)));
}

void test_tap_only_emits_at_release() {
  using namespace round_ui;
  GestureTracker tracker;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(true, 120, 100, 10).kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(true, 123, 101, 110).kind));
  Gesture event = tracker.update(false, 0, 0, 130);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::Tap), static_cast<int>(event.kind));
  TEST_ASSERT_EQUAL_INT(120, event.startX);
  TEST_ASSERT_EQUAL_INT(123, event.x);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 150).kind));
}

void test_swipe_over_pause_button_never_taps() {
  using namespace round_ui;
  GestureTracker tracker;
  tracker.update(true, 170, 100, 10);
  tracker.update(true, 110, 102, 90);
  tracker.update(true, 60, 100, 160);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::SwipeLeft), static_cast<int>(tracker.update(false, 0, 0, 180).kind));
  tracker.update(true, 60, 100, 300);
  tracker.update(true, 170, 101, 450);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::SwipeRight), static_cast<int>(tracker.update(false, 0, 0, 470).kind));
}

void test_vertical_diagonal_and_reversed_drag_do_not_tap() {
  using namespace round_ui;
  GestureTracker tracker;
  tracker.update(true, 120, 50, 0);
  tracker.update(true, 125, 180, 100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 120).kind));
  tracker.update(true, 60, 50, 200);
  tracker.update(true, 140, 160, 300);
  tracker.update(true, 150, 50, 350);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 400).kind));
  tracker.update(true, 120, 100, 500);
  tracker.update(true, 160, 100, 550);
  tracker.update(true, 120, 100, 600);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 650).kind));
}

void test_long_press_and_interrupted_contact_require_lift() {
  using namespace round_ui;
  GestureTracker tracker;
  tracker.update(true, 120, 100, 0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 800).kind));
  tracker.update(true, 120, 100, 900);
  tracker.cancel();
  tracker.update(true, 120, 100, 1000);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 1100).kind));
  tracker.update(true, 120, 100, 1200);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::Tap), static_cast<int>(tracker.update(false, 0, 0, 1250).kind));
  tracker.update(true, 120, 100, 2000);
  tracker.update(true, 120, 100, 3501);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 3600).kind));
}

void test_gesture_thresholds_and_clock_rollover() {
  using namespace round_ui;
  GestureTracker tracker;
  tracker.update(true, 170, 100, UINT32_MAX - 100U);
  tracker.update(true, 142, 100, 20);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::SwipeLeft), static_cast<int>(tracker.update(false, 0, 0, 40).kind));
  tracker.update(true, 120, 100, 100);
  tracker.update(true, 147, 100, 180);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 200).kind));
  tracker.update(true, 120, 100, 300);
  tracker.update(true, 128, 108, 400);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::Tap), static_cast<int>(tracker.update(false, 0, 0, 450).kind));
}

void test_idle_read_error_does_not_swallow_next_tap() {
  using namespace round_ui;
  GestureTracker tracker;
  tracker.cancel();
  tracker.update(true, 120, 100, 100);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::Tap), static_cast<int>(tracker.update(false, 0, 0, 180).kind));
  tracker.cancel(true);
  tracker.update(true, 120, 100, 300);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(GestureKind::None), static_cast<int>(tracker.update(false, 0, 0, 400).kind));
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

void test_dashboard_url_requires_a_local_address_and_network() {
  using namespace round_ui;
  char url[24];
  TEST_ASSERT_TRUE(dashboardUrl(true, false, "192.168.31.107", url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("http://192.168.31.107/", url);
  TEST_ASSERT_TRUE(dashboardUrl(false, true, "192.168.4.1", url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/", url);
  TEST_ASSERT_TRUE(dashboardUrl(true, false, "223.255.255.255", url, sizeof(url)));
  TEST_ASSERT_FALSE(dashboardUrl(false, false, "192.168.31.107", url, sizeof(url)));
  TEST_ASSERT_EQUAL_STRING("", url);
  const char* invalid[] = {"", "0.0.0.0", "255.255.255.255", "192.168.4", "1.2.3.256",
                          "1.2.3.4/path", "1.2.3.4:80", "a.b.c.d", "1.2.3.4444", "1..3.4"};
  for (const char* ip : invalid) {
    TEST_ASSERT_FALSE(dashboardUrl(true, false, ip, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING("", url);
  }
  TEST_ASSERT_FALSE(dashboardUrl(true, false, "192.168.4.1", url, 10));
  TEST_ASSERT_EQUAL_STRING("", url);
}

round_ui::Gesture dashboardTap() {
  round_ui::Gesture tap;
  tap.kind = round_ui::GestureKind::Tap;
  tap.startX = tap.x = 120;
  tap.startY = tap.y = 190;
  return tap;
}

void test_qr_modal_open_dismiss_and_offline_behaviour() {
  using namespace round_ui;
  Snapshot snapshot;
  snapshot.connected = true;
  std::strcpy(snapshot.ip, "192.168.31.107");
  Navigation navigation;
  navigation.page = Page::Network;
  const Gesture tap = dashboardTap();
  navigation.handle(tap, snapshot, false);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(navigation.handle(tap, snapshot, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::Dashboard), static_cast<int>(navigation.qrView));
  Gesture swipe; swipe.kind = GestureKind::SwipeLeft;
  navigation.handle(swipe, snapshot, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Network), static_cast<int>(navigation.page));
  navigation.handle(tap, snapshot, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::Dashboard), static_cast<int>(navigation.qrView));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(navigation.handle(tap, snapshot, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  navigation.handle(tap, snapshot, true);
  snapshot.connected = false;
  navigation.reconcile(snapshot);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  navigation.handle(tap, snapshot, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  snapshot.portal = true;
  navigation.handle(tap, snapshot, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::Dashboard), static_cast<int>(navigation.qrView));
  navigation.page = Page::Network;
  navigation.qrView = QrView::None;
  Gesture cross = tap; cross.startY = 150;
  navigation.handle(cross, snapshot, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
}

void test_qr_gesture_cannot_trigger_controls() {
  using namespace round_ui;
  Snapshot paused; paused.blocking = false;
  Navigation navigation;
  navigation.page = Page::Controls;
  navigation.qrView = QrView::Dashboard;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(navigation.handle(dashboardTap(), paused, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  navigation.page = Page::Network;
  Gesture swipe; swipe.kind = GestureKind::SwipeLeft;
  navigation.handle(swipe, paused, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Controls), static_cast<int>(navigation.page));
}

void test_setup_wifi_qr_opens_once_then_advances_to_portal() {
  using namespace round_ui;
  Snapshot setup;
  setup.portal = true;
  std::strcpy(setup.ap, "C3-AdBlock-ABCD");
  std::strcpy(setup.ip, "192.168.4.1");
  Navigation navigation;
  Snapshot pending = setup; pending.ap[0] = '\0';
  navigation.reconcile(pending);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  navigation.reconcile(setup);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Network), static_cast<int>(navigation.page));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::SetupWifi), static_cast<int>(navigation.qrView));
  navigation.handle(dashboardTap(), setup, false);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::SetupWifi), static_cast<int>(navigation.qrView));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Action::None),
                        static_cast<int>(navigation.handle(dashboardTap(), setup, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::Dashboard), static_cast<int>(navigation.qrView));
  navigation.handle(dashboardTap(), setup, true);
  navigation.reconcile(setup);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));

  Gesture wifiTap = dashboardTap(); wifiTap.startY = wifiTap.y = 155;
  navigation.handle(wifiTap, setup, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::SetupWifi), static_cast<int>(navigation.qrView));
  Gesture swipe; swipe.kind = GestureKind::SwipeLeft;
  navigation.handle(swipe, setup, true);
  navigation.reconcile(setup);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Network), static_cast<int>(navigation.page));
  navigation.handle(wifiTap, setup, true);
  setup.portal = false;
  setup.connected = true;
  navigation.reconcile(setup);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  navigation.handle(wifiTap, setup, true);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::None), static_cast<int>(navigation.qrView));
  setup.portal = true;
  navigation.reconcile(setup);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrView::SetupWifi), static_cast<int>(navigation.qrView));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_hit_testing);
  RUN_TEST(test_cyclic_pages);
  RUN_TEST(test_pet_home_and_status_are_presentation_only);
  RUN_TEST(test_tap_only_emits_at_release);
  RUN_TEST(test_swipe_over_pause_button_never_taps);
  RUN_TEST(test_vertical_diagonal_and_reversed_drag_do_not_tap);
  RUN_TEST(test_long_press_and_interrupted_contact_require_lift);
  RUN_TEST(test_gesture_thresholds_and_clock_rollover);
  RUN_TEST(test_idle_read_error_does_not_swallow_next_tap);
  RUN_TEST(test_percentage_widens_counters);
  RUN_TEST(test_refresh_wraparound);
  RUN_TEST(test_controls_repaint_on_pause_resume_and_portal);
  RUN_TEST(test_dashboard_url_requires_a_local_address_and_network);
  RUN_TEST(test_qr_modal_open_dismiss_and_offline_behaviour);
  RUN_TEST(test_qr_gesture_cannot_trigger_controls);
  RUN_TEST(test_setup_wifi_qr_opens_once_then_advances_to_portal);
  return UNITY_END();
}
