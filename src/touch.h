#pragma once

#include <stdint.h>

#include "display.h"
#include "touch_model.h"

namespace round_ui::touch {

bool begin();
bool ready();
Gesture poll(uint32_t now);

}  // namespace round_ui::touch
