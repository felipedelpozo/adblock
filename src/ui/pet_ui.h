#pragma once

#include <stdint.h>

#include "pet/PetEngine.h"

namespace round_ui::pet_ui {

// The display owns the clipping region and the LovyanGFX instance. Pet UI
// drawing is expressed through these tiny callbacks so it can use the same
// 240px logical geometry without allocating a framebuffer.
struct Canvas {
  void* context = nullptr;
  void (*fillRect)(void*, int32_t, int32_t, int32_t, int32_t, uint16_t) = nullptr;
  void (*fillRoundRect)(void*, int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) = nullptr;
  void (*outlineRoundRect)(void*, int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) = nullptr;
  void (*fillCircle)(void*, int32_t, int32_t, int32_t, uint16_t) = nullptr;
  void (*line)(void*, int32_t, int32_t, int32_t, int32_t, uint16_t) = nullptr;
  void (*text)(void*, const char*, int32_t, int32_t, uint16_t, uint8_t) = nullptr;
  void (*textCentered)(void*, const char*, int32_t, int32_t, uint16_t, uint8_t) = nullptr;
  // Optional local asset renderer. State order: idle, reaction, feeding, sleeping.
  // Return false to use the procedural evolution sprite instead.
  bool (*sprite)(void*, uint32_t, uint8_t) = nullptr;
};

struct Palette {
  uint16_t background;
  uint16_t panel;
  uint16_t panelRaised;
  uint16_t ink;
  uint16_t muted;
  uint16_t teal;
  uint16_t tealDim;
  uint16_t coral;
  uint16_t amber;
  uint16_t pet;
  uint16_t petShadow;
  uint16_t petAccent;
};

const char* speciesName(pet::Species species, bool english);

void drawHome(const Canvas& canvas, const Palette& palette, const pet::Snapshot& snapshot,
              uint32_t animationTimeMs, uint32_t rewardAmount, uint32_t rewardUntilMs,
              uint32_t reactionUntilMs, bool english, const char* name = "Adagotchi");

void drawStatus(const Canvas& canvas, const Palette& palette, const pet::Snapshot& snapshot,
                bool english);

}  // namespace round_ui::pet_ui
