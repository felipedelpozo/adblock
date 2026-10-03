#pragma once

#include <stdint.h>

#include "display.h"

namespace round_ui::touch {

struct Point {
  int16_t x = 0;
  int16_t y = 0;
  bool valid = false;
};

bool begin();
bool ready();
Point poll(uint32_t now);

}  // namespace round_ui::touch
