#pragma once

#include <stdint.h>
#include "ui_model.h"

namespace round_ui::display_qr {
constexpr int kVersion = 2;
constexpr int kSize = 25;
constexpr int kQuietZone = 4;
constexpr int kModulePixels = 4;
constexpr int kLeft = 54;
constexpr int kTop = 46;
// Cache a 79-byte bit matrix only when its URL changes, outside stripe draws.
bool prepare(const Snapshot& snapshot);
bool available();
bool dark(int x, int y);
}  // namespace round_ui::display_qr
