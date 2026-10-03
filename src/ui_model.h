#pragma once

#include <stdint.h>
#include "dashboard_link.h"
#include "touch_model.h"

namespace round_ui {

enum class Page : uint8_t { Status, Activity, Lists, Network, Controls };
constexpr uint8_t kPageCount = 5;

inline Page adjacentPage(Page page, int8_t direction) {
  const int16_t index = static_cast<uint8_t>(page);
  return static_cast<Page>((index + (direction < 0 ? kPageCount - 1 : 1)) % kPageCount);
}

enum class Action : uint8_t {
  None = 0,
  Pause5Minutes,
  Pause30Minutes,
  Resume,
};

struct Snapshot {
  bool blocking = true;
  bool connected = false;
  bool portal = false;
  uint32_t blocked = 0;
  uint32_t allowed = 0;
  uint32_t domains = 0;
  uint32_t customDomains = 0;
  uint32_t resumeSeconds = 0;
  uint16_t clients = 0;
  int32_t rssi = 0;
  char ip[16] = {};
  char ap[24] = {};
};

inline bool controlStateChanged(const Snapshot& previous, const Snapshot& next) {
  return previous.blocking != next.blocking || previous.portal != next.portal;
}

// The counters can be close to UINT32_MAX on a long-running appliance. Do the
// multiplication after widening so the percentage never wraps at 2^32.
inline uint8_t blockedPercent(uint32_t blocked, uint32_t allowed) {
  const uint64_t total = static_cast<uint64_t>(blocked) + allowed;
  if (total == 0) return 0;
  const uint64_t value = (static_cast<uint64_t>(blocked) * 100ULL) / total;
  return static_cast<uint8_t>(value > 100 ? 100 : value);
}

inline uint16_t blockedPercentTenths(uint32_t blocked, uint32_t allowed) {
  const uint64_t total = static_cast<uint64_t>(blocked) + allowed;
  return total ? static_cast<uint16_t>((static_cast<uint64_t>(blocked) * 1000ULL) / total) : 0;
}

// Unsigned subtraction intentionally gives correct elapsed time across the
// uint32_t millis() wraparound boundary.
inline bool refreshDue(uint32_t now, uint32_t last, uint32_t interval) {
  return static_cast<uint32_t>(now - last) >= interval;
}

struct ControlRect {
  int16_t x, y, width, height;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};

// Logical coordinates are shared by the 240px and 360px panels.
constexpr ControlRect kPause5Button = {50, 91, 140, 34};
constexpr ControlRect kPause30Button = {50, 132, 140, 34};
constexpr ControlRect kResumeButton = {58, 173, 124, 27};
constexpr ControlRect kDashboardButton = {54, 181, 132, 27};

inline Action hitTest(Page page, bool blocking, bool portal, int16_t x, int16_t y) {
  if (page != Page::Controls || portal) return Action::None;
  if (kPause5Button.contains(x, y)) return Action::Pause5Minutes;
  if (kPause30Button.contains(x, y)) return Action::Pause30Minutes;
  if (!blocking && kResumeButton.contains(x, y)) return Action::Resume;
  return Action::None;
}

inline bool dashboardAvailable(const Snapshot& snapshot) {
  return (snapshot.connected || snapshot.portal) && validDashboardIp(snapshot.ip);
}

class Navigation {
 public:
  Page page = Page::Status;
  bool qrVisible = false;

  void reconcile(const Snapshot& snapshot) {
    if (!dashboardAvailable(snapshot)) qrVisible = false;
  }

  Action handle(const Gesture& gesture, const Snapshot& snapshot, bool pageReady) {
    if (gesture.kind == GestureKind::None) return Action::None;
    if (qrVisible) {
      // Dismissal consumes the complete gesture; it cannot also change pages
      // or activate a pause button behind the modal.
      qrVisible = false;
      return Action::None;
    }
    if (gesture.kind == GestureKind::SwipeLeft || gesture.kind == GestureKind::SwipeRight) {
      page = adjacentPage(page, gesture.kind == GestureKind::SwipeLeft ? 1 : -1);
      return Action::None;
    }
    if (!pageReady) return Action::None;
    if (page == Page::Network && dashboardAvailable(snapshot) &&
        kDashboardButton.contains(gesture.startX, gesture.startY) &&
        kDashboardButton.contains(gesture.x, gesture.y)) {
      qrVisible = true;
      return Action::None;
    }
    const Action start = hitTest(page, snapshot.blocking, snapshot.portal,
                                gesture.startX, gesture.startY);
    const Action end = hitTest(page, snapshot.blocking, snapshot.portal, gesture.x, gesture.y);
    return start == end ? start : Action::None;
  }
};

}  // namespace round_ui
