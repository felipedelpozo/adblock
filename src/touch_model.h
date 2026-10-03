#pragma once

#include <stdint.h>

namespace round_ui {

enum class GestureKind : uint8_t { None, Tap, SwipeLeft, SwipeRight };

struct Gesture {
  GestureKind kind = GestureKind::None;
  int16_t startX = 0;
  int16_t startY = 0;
  int16_t x = 0;
  int16_t y = 0;
};

// One event at release, so a swipe that starts over a button cannot pause DNS.
// Distances are in logical 240px coordinates, independent of the touch panel.
class GestureTracker {
 public:
  Gesture update(bool touching, int16_t x, int16_t y, uint32_t now) {
    if (cancelled_) {
      if (!touching) cancelled_ = false;
      return {};
    }
    if (touching) {
      if (!down_) {
        down_ = true;
        startedAt_ = now;
        startX_ = x;
        startY_ = y;
        maxX_ = maxY_ = 0;
      }
      lastX_ = x;
      lastY_ = y;
      const int16_t dx = distance(x, startX_);
      const int16_t dy = distance(y, startY_);
      if (dx > maxX_) maxX_ = dx;
      if (dy > maxY_) maxY_ = dy;
      if (static_cast<uint32_t>(now - startedAt_) > kSwipeMaxMs) cancel();
      return {};
    }
    if (!down_) return {};
    down_ = false;
    Gesture event;
    event.startX = startX_;
    event.startY = startY_;
    event.x = lastX_;
    event.y = lastY_;
    const uint32_t duration = now - startedAt_;
    const int16_t dx = lastX_ - startX_;
    const int16_t horizontal = distance(lastX_, startX_);
    // Use the largest vertical excursion, not just the endpoint, to reject
    // curved/vertical gestures that happen to finish at their starting height.
    if (duration <= kSwipeMaxMs && horizontal >= kSwipeDistance &&
        horizontal * 2 >= maxY_ * 3) {
      event.kind = dx < 0 ? GestureKind::SwipeLeft : GestureKind::SwipeRight;
    } else if (duration <= kTapMaxMs && maxX_ <= kTapSlop && maxY_ <= kTapSlop) {
      event.kind = GestureKind::Tap;
    }
    return event;
  }

  // Loss of contact samples is not a release. Require a confirmed lift after
  // an I2C failure or invalid coordinate before accepting another gesture.
  void cancel(bool contactKnown = false) {
    if (down_ || contactKnown) cancelled_ = true;
    down_ = false;
  }
  void reset() { down_ = false; cancelled_ = false; }

 private:
  static constexpr int16_t kSwipeDistance = 28;
  static constexpr int16_t kTapSlop = 8;
  static constexpr uint32_t kTapMaxMs = 700;
  static constexpr uint32_t kSwipeMaxMs = 1500;
  static int16_t distance(int16_t a, int16_t b) { return a >= b ? a - b : b - a; }
  bool down_ = false;
  bool cancelled_ = false;
  uint32_t startedAt_ = 0;
  int16_t startX_ = 0, startY_ = 0, lastX_ = 0, lastY_ = 0;
  int16_t maxX_ = 0, maxY_ = 0;
};

}  // namespace round_ui
