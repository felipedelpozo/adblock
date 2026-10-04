#include "pet_ui.h"

#include <cstdio>

namespace {

using round_ui::pet_ui::Canvas;
using round_ui::pet_ui::Palette;

void rect(const Canvas& c, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  if (c.fillRect) c.fillRect(c.context, x, y, w, h, color);
}

void roundRect(const Canvas& c, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
               uint16_t color) {
  if (c.fillRoundRect) c.fillRoundRect(c.context, x, y, w, h, r, color);
}

void outline(const Canvas& c, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r,
             uint16_t color) {
  if (c.outlineRoundRect) c.outlineRoundRect(c.context, x, y, w, h, r, color);
}

void circle(const Canvas& c, int32_t x, int32_t y, int32_t r, uint16_t color) {
  if (c.fillCircle) c.fillCircle(c.context, x, y, r, color);
}

void stroke(const Canvas& c, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) {
  if (c.line) c.line(c.context, x1, y1, x2, y2, color);
}

void label(const Canvas& c, const char* value, int32_t x, int32_t y, uint16_t color,
           uint8_t size = 1) {
  if (c.text) c.text(c.context, value, x, y, color, size);
}

void centered(const Canvas& c, const char* value, int32_t x, int32_t y, uint16_t color,
              uint8_t size = 1) {
  if (c.textCentered) c.textCentered(c.context, value, x, y, color, size);
}

const char* text(bool english, const char* en, const char* es) { return english ? en : es; }

uint8_t bounded(uint8_t value) { return value > 100 ? 100 : value; }

void number(uint32_t value, char* output, size_t capacity) {
  // Keep counters within four glyphs at the narrow bottom of the circle.
  const uint32_t unit = value >= 1000000000U ? 1000000000U : value >= 1000000U ? 1000000U : 1000U;
  const char* suffix = unit == 1000000000U ? "B" : unit == 1000000U ? "M" : "K";
  if (value < 1000U) std::snprintf(output, capacity, "%lu", static_cast<unsigned long>(value));
  else if (value / unit >= 10U)
    std::snprintf(output, capacity, "%lu%s", static_cast<unsigned long>(value / unit), suffix);
  else std::snprintf(output, capacity, "%lu.%lu%s", static_cast<unsigned long>(value / unit),
                    static_cast<unsigned long>((value % unit) / (unit / 10U)), suffix);
}

void drawBar(const Canvas& c, int32_t y, const char* name, uint8_t value, uint16_t color,
            const Palette& p) {
  char amount[8];
  std::snprintf(amount, sizeof(amount), "%u%%", static_cast<unsigned>(bounded(value)));
  label(c, name, 35, y, p.muted, 1);
  roundRect(c, 95, y + 1, 96, 8, 4, p.panel);
  roundRect(c, 95, y + 1, static_cast<int32_t>(96U * bounded(value) / 100U), 8, 4, color);
  label(c, amount, 195, y, p.ink, 1);
}

void drawMiniBar(const Canvas& c, int32_t x, int32_t y, const char* name, uint8_t value,
                 uint16_t color, const Palette& p) {
  label(c, name, x, y, p.muted, 1);
  roundRect(c, x + 28, y + 1, 36, 7, 3, p.panel);
  roundRect(c, x + 28, y + 1, static_cast<int32_t>(36U * bounded(value) / 100U), 7, 3, color);
}

bool beforeDeadline(uint32_t now, uint32_t deadline) {
  return deadline != 0 && static_cast<int32_t>(now - deadline) < 0;
}

void drawPet(const Canvas& c, const Palette& p, pet::Species species, uint32_t animationTimeMs,
             bool reacting) {
  const uint32_t frame = (animationTimeMs / 250U) & 1U;
  const int32_t bob = frame == 0 ? 0 : (reacting ? -3 : -1);
  const int32_t cy = 115 + bob;
  const bool egg = species == pet::Species::Egg;
  const bool voidPet = species == pet::Species::Void;
  const uint16_t body = voidPet ? p.panelRaised : (egg ? p.amber : p.pet);
  const uint16_t accent = voidPet ? p.coral : (egg ? p.petShadow : p.petAccent);

  if (egg) {
    circle(c, 120, cy, 31, body);
    circle(c, 120, cy - 2, 24, p.panel);
    stroke(c, 100, cy - 12, 108, cy - 4, accent);
    stroke(c, 108, cy - 4, 102, cy + 6, accent);
    stroke(c, 136, cy - 15, 130, cy - 3, accent);
    stroke(c, 130, cy - 3, 137, cy + 8, accent);
    centered(c, reacting ? "..." : "o o", 120, cy + 8, p.ink, 2);
    return;
  }

  if (voidPet) {
    circle(c, 120, cy, 34, body);
    circle(c, 109, cy - 4, 4, p.ink);
    circle(c, 131, cy - 4, 4, p.ink);
    circle(c, 109, cy - 4, 2, accent);
    circle(c, 131, cy - 4, 2, accent);
    stroke(c, 112, cy + 13, 128, cy + 13, p.muted);
    return;
  }

  // Every post-hatch stage shares one deliberately blocky silhouette. The
  // accent and expression communicate progression while keeping the renderer
  // deterministic and cheap on both round panels.
  roundRect(c, 84, cy - 28, 72, 59, 14, body);
  roundRect(c, 92, cy - 47, 18, 25, 7, body);
  roundRect(c, 130, cy - 47, 18, 25, 7, body);
  circle(c, 105, cy - 7, 6, p.ink);
  circle(c, 135, cy - 7, 6, p.ink);
  circle(c, 105, cy - 7, 2, p.panel);
  circle(c, 135, cy - 7, 2, p.panel);
  if (reacting) {
    stroke(c, 108, cy + 13, 120, cy + 18, accent);
    stroke(c, 120, cy + 18, 132, cy + 13, accent);
  } else {
    stroke(c, 111, cy + 14, 129, cy + 14, accent);
  }
  if (species == pet::Species::AdHunter || species == pet::Species::AdEater) {
    stroke(c, 89, cy - 24, 81, cy - 34, accent);
    stroke(c, 151, cy - 24, 159, cy - 34, accent);
  }
  if (species == pet::Species::AdEater) {
    circle(c, 120, cy + 1, 4, accent);
  }
}

}  // namespace

namespace round_ui::pet_ui {

const char* speciesName(pet::Species species, bool english) {
  switch (species) {
    case pet::Species::Egg: return text(english, "Egg", "Huevo");
    case pet::Species::Hatchling: return text(english, "Hatchling", "Cria");
    case pet::Species::Blocky: return text(english, "Blocky", "Blocky");
    case pet::Species::AdHunter: return text(english, "AdHunter", "Caza anuncios");
    case pet::Species::AdEater: return text(english, "AdEater", "Come anuncios");
    case pet::Species::Void: return text(english, "Void", "Vacio");
  }
  return text(english, "Adagotchi", "Adagotchi");
}

void drawHome(const Canvas& c, const Palette& p, const pet::Snapshot& snapshot,
              uint32_t animationTimeMs, uint32_t rewardAmount, uint32_t rewardUntilMs,
              uint32_t reactionUntilMs, bool english, const char* name) {
  centered(c, name, 120, 25, p.muted, 1);
  const bool reacting = beforeDeadline(animationTimeMs, rewardUntilMs) ||
                        beforeDeadline(animationTimeMs, reactionUntilMs);
  const uint8_t state = beforeDeadline(animationTimeMs, rewardUntilMs) ? 2 :
      beforeDeadline(animationTimeMs, reactionUntilMs) ? 1 : snapshot.energy <= 30 ? 3 : 0;
  if (!c.sprite || !c.sprite(c.context, animationTimeMs, state))
    drawPet(c, p, snapshot.species, animationTimeMs, reacting);

  char level[20];
  char food[20];
  number(snapshot.foodToday, food, sizeof(food));
  std::snprintf(level, sizeof(level), "%s %u", text(english, "LV", "NIV"),
                static_cast<unsigned>(snapshot.level));
  centered(c, speciesName(snapshot.species, english), 120, 151, p.ink, 2);
  centered(c, level, 120, 168, p.teal, 1);
  drawMiniBar(c, 35, 183, text(english, "HUN", "HAM"), snapshot.hunger, p.amber, p);
  drawMiniBar(c, 128, 183, text(english, "ENE", "ENE"), snapshot.energy, p.coral, p);
  char blocked[20];
  number(snapshot.blockedToday, blocked, sizeof(blocked));
  char foodDay[20];
  std::snprintf(foodDay, sizeof(foodDay), "%s/%s", food,
                text(english, "DAY", "DIA"));
  centered(c, foodDay, 80, 200, p.amber, 1);
  char blockedDay[24];
  std::snprintf(blockedDay, sizeof(blockedDay), "%s %s", blocked,
                text(english, "BLK", "BLOQ"));
  centered(c, blockedDay, 160, 200, p.coral, 1);

  if (rewardAmount > 0 && beforeDeadline(animationTimeMs, rewardUntilMs)) {
    char reward[20];
    std::snprintf(reward, sizeof(reward), "+%lu %s", static_cast<unsigned long>(rewardAmount),
                  text(english, "FOOD", "COMIDA"));
    roundRect(c, 72, 42, 96, 22, 8, p.tealDim);
    outline(c, 72, 42, 96, 22, 8, p.teal);
    centered(c, reward, 120, 48, p.ink, 1);
  }
}

void drawStatus(const Canvas& c, const Palette& p, const pet::Snapshot& snapshot, bool english) {
  centered(c, text(english, "PET STATUS", "ESTADO MASCOTA"), 120, 15, p.muted, 1);
  centered(c, speciesName(snapshot.species, english), 120, 35, p.ink, 2);
  char level[20];
  std::snprintf(level, sizeof(level), "%s %u", text(english, "LEVEL", "NIVEL"),
                static_cast<unsigned>(snapshot.level));
  centered(c, level, 120, 57, p.teal, 1);
  char progress[32], xp[8], totalFood[8];
  number(snapshot.xp, xp, sizeof(xp));
  number(snapshot.totalFood, totalFood, sizeof(totalFood));
  std::snprintf(progress, sizeof(progress), "XP %s  %s %s", xp,
                text(english, "FOOD", "COMIDA"), totalFood);
  centered(c, progress, 120, 69, p.muted, 1);
  drawBar(c, 82, text(english, "HUNGER", "HAMBRE"), snapshot.hunger, p.amber, p);
  drawBar(c, 108, text(english, "HAPPY", "ANIMO"), snapshot.happiness, p.teal, p);
  drawBar(c, 134, text(english, "ENERGY", "ENERGIA"), snapshot.energy, p.coral, p);

  char blocked[20];
  char allowed[20];
  number(snapshot.blockedToday, blocked, sizeof(blocked));
  number(snapshot.allowedToday, allowed, sizeof(allowed));
  centered(c, blocked, 74, 174, p.coral, 2);
  centered(c, allowed, 166, 174, p.teal, 2);
  centered(c, text(english, "BLOCKED", "BLOQ."), 74, 198, p.muted, 1);
  centered(c, text(english, "ALLOWED", "PERM."), 166, 198, p.muted, 1);
}

}  // namespace round_ui::pet_ui
