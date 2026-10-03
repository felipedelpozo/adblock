#pragma once

#include <stdint.h>

#include "ui_model.h"

namespace round_ui {

// Call begin once from setup, poll frequently from loop, and update once per
// loop with the latest appliance snapshot. poll returns one edge-triggered
// action; it never changes the blocking state itself.
bool begin();
Action poll(uint32_t now);
void update(const Snapshot& snapshot, uint32_t now);

}  // namespace round_ui
