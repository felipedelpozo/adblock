#include "ui.h"

#include <Arduino.h>

#include "display.h"
#include "touch.h"

namespace {

constexpr uint32_t kSampleIntervalMs = 250;
bool initialized = false;
bool sampled = false;
uint32_t lastSampleAt = 0;

const char* actionName(round_ui::Action action) {
  switch (action) {
    case round_ui::Action::Pause5Minutes: return "pause5";
    case round_ui::Action::Pause30Minutes: return "pause30";
    case round_ui::Action::Resume: return "resume";
    case round_ui::Action::None: break;
  }
  return "none";
}

}  // namespace

namespace round_ui {

bool begin() {
  initialized = true;
  sampled = false;
  lastSampleAt = 0;
#if defined(ROUND_DISPLAY)
  const bool displayOk = display::begin();
  const bool touchOk = touch::begin();
  Serial.printf("[round-ui] display=%s touch=%s max_render_us=%lu free_heap=%lu\n",
                displayOk ? "ok" : "failed", touchOk ? "scheduled" : "stub",
                static_cast<unsigned long>(display::maxRenderMicros()),
                static_cast<unsigned long>(ESP.getFreeHeap()));
  return displayOk && touchOk;
#else
  return false;
#endif
}

Action poll(uint32_t now) {
  if (!initialized) return Action::None;
  const touch::Point point = touch::poll(now);
  if (!point.valid) return Action::None;
  const Action action = hitTest(point.x, point.y);
#if defined(ROUND_DISPLAY)
  Serial.printf("[round-ui] touch x=%d y=%d action=%s\n", point.x, point.y, actionName(action));
#endif
  return action;
}

void update(const Snapshot& snapshot, uint32_t now) {
  if (!initialized) return;
  if (!sampled || refreshDue(now, lastSampleAt, kSampleIntervalMs)) {
    display::setSnapshot(snapshot);
    sampled = true;
    lastSampleAt = now;
  }
  // Display rendering is incremental: at most one clipped stripe is written here.
  display::renderOneRegion(now);
#if defined(ROUND_DISPLAY)
  static uint32_t lastDiagnostic = 0;
  if (now - lastDiagnostic >= 15000) {
    lastDiagnostic = now;
    Serial.printf("[round-ui] touch=%s render_max_us=%lu heap=%lu wifi=%s ip=%s\n",
        touch::ready() ? "ready" : "unavailable",
        static_cast<unsigned long>(display::maxRenderMicros()),
        static_cast<unsigned long>(ESP.getFreeHeap()),
        snapshot.connected ? "connected" : (snapshot.portal ? "portal" : "offline"), snapshot.ip);
  }
#endif
}

}  // namespace round_ui
