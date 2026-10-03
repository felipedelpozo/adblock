#pragma once
#include <stdint.h>

// Elapsed-time arithmetic stays correct across millis() rollover, including
// when a deadline would equal zero. Pauses are intentionally not persisted.
class BlockingState {
 public:
  bool active() const { return active_; }
  void pause(uint32_t now, uint32_t seconds) {
    active_ = false;
    startedAt_ = now;
    // Bound web-supplied durations to signed half-range for predictable timers.
    durationMs_ = seconds > 2147483U ? 2147483000U : seconds * 1000U;
  }
  void resume() { active_ = true; durationMs_ = 0; }
  void tick(uint32_t now) {
    if (!active_ && durationMs_ && now - startedAt_ >= durationMs_) resume();
  }
  uint32_t remainingSeconds(uint32_t now) const {
    if (active_ || !durationMs_) return 0;
    const uint32_t elapsed = now - startedAt_;
    return elapsed >= durationMs_ ? 0 : (durationMs_ - elapsed + 999U) / 1000U;
  }
 private:
  bool active_ = true;
  uint32_t startedAt_ = 0;
  uint32_t durationMs_ = 0;
};
