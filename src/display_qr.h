#pragma once

#include <stdint.h>
#include "ui_model.h"

namespace round_ui::display_qr {
constexpr int kVersion = 2;
constexpr int kSize = 25;
constexpr int kSetupVersion = 3;
constexpr int kMaxSize = 29;
constexpr int kQuietZone = 4;
constexpr int kModulePixels = 4;
constexpr int kLeft = 54;
constexpr int kTop = 46;
// Cache one packed matrix (at most 106 bytes) only when its view or payload
// changes, outside stripe draws.
bool prepare(const Snapshot& snapshot, QrView view = QrView::Dashboard);
bool available();
int size();
bool dark(int x, int y);
}  // namespace round_ui::display_qr
