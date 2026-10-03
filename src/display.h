#pragma once

#include <stdint.h>

#include "ui_model.h"

namespace round_ui {
namespace pins {
#if defined(ROUND_DISPLAY_S3)
// Guition JC3636W518C, confirmed against the manufacturer demo and schematics.
constexpr int kDisplaySclk = 9;
constexpr int kDisplayCs = 10;
constexpr int kDisplayBacklight = 15;
constexpr int kDisplayReset = 47;
constexpr int kTouchSda = 7;
constexpr int kTouchScl = 8;
constexpr int kTouchReset = 40;
constexpr int kTouchInterrupt = 41;
constexpr int kPanelSize = 360;
#else
// ESP32-2424S012C wiring. The panel has no usable LCD reset connection.
constexpr int kDisplaySclk = 6;
constexpr int kDisplayMosi = 7;
constexpr int kDisplayDc = 2;
constexpr int kDisplayCs = 10;
constexpr int kDisplayBacklight = 3;
constexpr int kDisplayReset = -1;

constexpr int kTouchSda = 4;
constexpr int kTouchScl = 5;
constexpr int kTouchReset = 1;
constexpr int kTouchInterrupt = 0;
constexpr int kPanelSize = 240;
#endif
constexpr uint8_t kTouchAddress = 0x15;
}  // namespace pins

namespace display {

bool begin();
// Select the page shown by the round display. A page change cancels the
// current stripe and starts a complete repaint from the new page snapshot.
void setPage(Page page);
void setDashboardQr(bool visible);
bool pageReady();
void setSnapshot(const Snapshot& snapshot);
// Draw at most one clipped stripe. Returns true when a stripe was drawn.
bool renderOneRegion(uint32_t now);
uint32_t maxRenderMicros();

}  // namespace display
}  // namespace round_ui
