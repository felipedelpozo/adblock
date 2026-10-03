#pragma once

#include <stdint.h>

namespace round_ui {

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

// The counters can be close to UINT32_MAX on a long-running appliance. Do the
// multiplication after widening so the percentage never wraps at 2^32.
inline uint8_t blockedPercent(uint32_t blocked, uint32_t allowed) {
  const uint64_t total = static_cast<uint64_t>(blocked) + allowed;
  if (total == 0) return 0;
  const uint64_t value = (static_cast<uint64_t>(blocked) * 100ULL) / total;
  return static_cast<uint8_t>(value > 100 ? 100 : value);
}

// Unsigned subtraction intentionally gives correct elapsed time across the
// uint32_t millis() wraparound boundary.
inline bool refreshDue(uint32_t now, uint32_t last, uint32_t interval) {
  return static_cast<uint32_t>(now - last) >= interval;
}

// Coordinates are the calibrated 240x240 CST816 coordinate space. Keeping
// hit testing pure makes the interaction contract testable without hardware.
inline Action hitTest(int16_t x, int16_t y) {
  if (y < 174 || y >= 206) return Action::None;
  if (x >= 38 && x < 88) return Action::Pause5Minutes;
  if (x >= 95 && x < 145) return Action::Pause30Minutes;
  if (x >= 152 && x < 202) return Action::Resume;
  return Action::None;
}

}  // namespace round_ui
