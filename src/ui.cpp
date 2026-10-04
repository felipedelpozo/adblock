#include "ui.h"

#include <Arduino.h>

#include "display.h"
#include "touch.h"

namespace {

constexpr uint32_t kSampleIntervalMs = 250;
bool initialized = false;
bool sampled = false;
uint32_t lastSampleAt = 0;
round_ui::Navigation navigation;
round_ui::Snapshot interactionState;

const char* pageName(round_ui::Page value) {
  switch (value) {
    case round_ui::Page::PetHome: return "pet-home";
    case round_ui::Page::Status: return "status";
    case round_ui::Page::Activity: return "activity";
    case round_ui::Page::Lists: return "lists";
    case round_ui::Page::Network: return "network";
    case round_ui::Page::Controls: return "controls";
    case round_ui::Page::PetStatus: return "pet-status";
  }
  return "unknown";
}

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
  navigation = Navigation{};
  interactionState = Snapshot{};
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
  const Gesture point = touch::poll(now);
  if (point.kind == GestureKind::None) return Action::None;
  const Page pageBeforeGesture = navigation.page;
  const Action action = navigation.handle(point, interactionState,
                                         sampled && display::pageReady());
  // A pet tap is deliberately presentation-only. The engine is fed from DNS
  // events in main; the display gets a short reaction without changing state.
  if (pageBeforeGesture == Page::PetHome && petTap(point) && sampled && display::pageReady() &&
      navigation.qrView == QrView::None) {
    display::reactPet(now);
  }
  display::setPage(navigation.page);
  display::setQrView(navigation.qrView);
  if (point.kind == GestureKind::SwipeLeft || point.kind == GestureKind::SwipeRight) {
#if defined(ROUND_DISPLAY)
    Serial.printf("[round-ui] swipe=%s page=%s qr=%s\n",
        point.kind == GestureKind::SwipeLeft ? "left" : "right", pageName(navigation.page),
        navigation.qrView == QrView::None ? "hidden" :
        navigation.qrView == QrView::SetupWifi ? "wifi" : "dashboard");
#endif
    return Action::None;
  }
#if defined(ROUND_DISPLAY)
  Serial.printf("[round-ui] tap x=%d y=%d page=%s action=%s qr=%s\n",
                point.x, point.y, pageName(navigation.page), actionName(action),
                navigation.qrView == QrView::None ? "hidden" :
                navigation.qrView == QrView::SetupWifi ? "wifi" : "dashboard");
#endif
  return action;
}

void update(const Snapshot& snapshot, uint32_t now) {
  if (!initialized) return;
  const bool portalChanged = interactionState.portal != snapshot.portal;
  interactionState = snapshot;
  navigation.reconcile(snapshot);
  display::setPage(navigation.page);
  if (!sampled || portalChanged || refreshDue(now, lastSampleAt, kSampleIntervalMs)) {
    Snapshot displaySnapshot = snapshot;
    displaySnapshot.animationTimeMs = now;
    display::setSnapshot(displaySnapshot);
    sampled = true;
    lastSampleAt = now;
  }
  display::setQrView(navigation.qrView);
  // Display rendering is incremental: at most one clipped stripe is written here.
  display::renderOneRegion(now);
#if defined(ROUND_DISPLAY)
  static uint32_t lastDiagnostic = 0;
  if (now - lastDiagnostic >= 15000) {
    lastDiagnostic = now;
    Serial.printf("[round-ui] page=%s qr=%s touch=%s render_max_us=%lu heap=%lu wifi=%s ip=%s\n",
        pageName(navigation.page),
        navigation.qrView == QrView::None ? "hidden" :
        navigation.qrView == QrView::SetupWifi ? "wifi" : "dashboard",
        touch::ready() ? "ready" : "unavailable",
        static_cast<unsigned long>(display::maxRenderMicros()),
        static_cast<unsigned long>(ESP.getFreeHeap()),
        snapshot.connected ? "connected" : (snapshot.portal ? "portal" : "offline"), snapshot.ip);
  }
#endif
}

}  // namespace round_ui
