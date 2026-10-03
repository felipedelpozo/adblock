#include "display.h"

#if defined(ROUND_DISPLAY)

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <cstdio>
#include <cstring>

#if defined(ROUND_DISPLAY_S3)
#include "st77916_qspi.h"
#endif

namespace {

#if !defined(ROUND_DISPLAY_S3)
class RoundDisplay final : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 panel_;
  lgfx::Bus_SPI bus_;

 public:
  RoundDisplay() {
    auto bus = bus_.config();
#if defined(SPI2_HOST)
    bus.spi_host = SPI2_HOST;
#endif
    bus.spi_mode = 0;
    bus.freq_write = 40000000;
    bus.freq_read = 16000000;
    bus.pin_sclk = round_ui::pins::kDisplaySclk;
    bus.pin_mosi = round_ui::pins::kDisplayMosi;
    bus.pin_miso = -1;
    bus.pin_dc = round_ui::pins::kDisplayDc;
    bus.dma_channel = SPI_DMA_CH_AUTO;
    bus_.config(bus);
    panel_.setBus(&bus_);

    auto panel = panel_.config();
    panel.pin_cs = round_ui::pins::kDisplayCs;
    panel.pin_rst = round_ui::pins::kDisplayReset;
    panel.pin_busy = -1;
    panel.memory_width = 240;
    panel.memory_height = 240;
    panel.panel_width = 240;
    panel.panel_height = 240;
    panel.offset_x = 0;
    panel.offset_y = 0;
    panel.offset_rotation = 0;
    panel.dlen_16bit = 0;
    panel.bus_shared = false;
    panel_.config(panel);
    setPanel(&panel_);
  }
};

#endif

#if defined(ROUND_DISPLAY_S3)
RoundDisplayS3 gfx;
#else
RoundDisplay gfx;
#endif

constexpr uint8_t kRegionCount = 4;
// Logical coordinates are always 240x240. The S3 panel scales each write to
// 360x360, so the same layout remains valid on the C3 round panel.
constexpr int32_t kRegionTop[kRegionCount + 1] = {0, 42, 114, 166, 240};
constexpr int32_t kSliceHeight = 8;

round_ui::Snapshot current;
round_ui::Snapshot paint;
bool activeChanged = false;
uint8_t dirtyRegions = 0x0f;
int8_t activeRegion = -1;
uint8_t nextRegion = 0;
int32_t sliceY = 0;
int32_t clipTop = 0;
int32_t clipBottom = 240;
uint32_t maxRenderUs = 0;
uint32_t renderCount = 0;

bool sameSnapshot(const round_ui::Snapshot& a, const round_ui::Snapshot& b) {
  return a.blocking == b.blocking && a.connected == b.connected && a.portal == b.portal &&
         a.blocked == b.blocked && a.allowed == b.allowed && a.domains == b.domains &&
         a.customDomains == b.customDomains && a.resumeSeconds == b.resumeSeconds &&
         a.clients == b.clients && a.rssi == b.rssi && std::strcmp(a.ip, b.ip) == 0 &&
         std::strcmp(a.ap, b.ap) == 0;
}

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint16_t>(((r & 0xf8U) << 8) | ((g & 0xfcU) << 3) | (b >> 3));
}

// The palette follows the dashboard's dark ink surface and teal success
// accent. Coral and amber are reserved for blocked traffic and portal state.
constexpr uint16_t kBackground = rgb565(7, 13, 18);
constexpr uint16_t kPanel = rgb565(15, 27, 35);
constexpr uint16_t kPanelRaised = rgb565(22, 40, 47);
constexpr uint16_t kInk = rgb565(232, 242, 242);
constexpr uint16_t kMuted = rgb565(143, 168, 169);
constexpr uint16_t kTeal = rgb565(45, 214, 180);
constexpr uint16_t kTealDim = rgb565(24, 91, 86);
constexpr uint16_t kCoral = rgb565(244, 111, 113);
constexpr uint16_t kCoralDim = rgb565(102, 45, 53);
constexpr uint16_t kAmber = rgb565(248, 190, 75);
constexpr uint16_t kBlue = rgb565(93, 173, 242);

int32_t scaled(int32_t value) { return value * round_ui::pins::kPanelSize / 240; }

bool intersects(int32_t y, int32_t height) {
  return y < clipBottom && y + height > clipTop;
}

void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  gfx.fillRect(scaled(x), scaled(y), scaled(w), scaled(h), color);
}

void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius,
                   uint16_t color) {
  if (!intersects(y, h)) return;
  gfx.fillRoundRect(scaled(x), scaled(y), scaled(w), scaled(h), scaled(radius), color);
}

void outlineRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius,
                      uint16_t color) {
  if (!intersects(y, h)) return;
  gfx.drawRoundRect(scaled(x), scaled(y), scaled(w), scaled(h), scaled(radius), color);
}

void circle(int32_t x, int32_t y, int32_t radius, uint16_t color) {
  if (!intersects(y - radius, radius * 2 + 1)) return;
  gfx.drawCircle(scaled(x), scaled(y), scaled(radius), color);
}

void line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) {
  const int32_t top = y1 < y2 ? y1 : y2;
  const int32_t bottom = y1 > y2 ? y1 : y2;
  if (!intersects(top, bottom - top + 1)) return;
  gfx.drawLine(scaled(x1), scaled(y1), scaled(x2), scaled(y2), color);
}

uint8_t physicalTextSize(uint8_t logicalSize) {
  // LovyanGFX's bitmap font is integer-sized. One extra step on 360px keeps
  // labels readable while retaining the same logical footprint on the C3.
  if (round_ui::pins::kPanelSize >= 360 && logicalSize > 0) return logicalSize + 1;
  return logicalSize;
}

int32_t textHeight(uint8_t logicalSize) {
  const uint8_t actualSize = physicalTextSize(logicalSize);
  // Bitmap glyphs are eight physical pixels tall. Convert that height back to
  // logical coordinates so the last clipped stripe still paints the glyph.
  return (8 * actualSize * 240 + round_ui::pins::kPanelSize - 1) /
             round_ui::pins::kPanelSize +
         1;
}

void text(const char* value, int32_t x, int32_t y, uint16_t color, uint8_t size = 1) {
  if (!value || !intersects(y, textHeight(size))) return;
  const uint8_t actualSize = physicalTextSize(size);
  gfx.setTextColor(color);
  gfx.setTextSize(actualSize);
  gfx.drawString(value, scaled(x), scaled(y));
}

void textCentered(const char* value, int32_t centerX, int32_t y, uint16_t color,
                 uint8_t size = 1) {
  if (!value) return;
  const uint8_t actualSize = physicalTextSize(size);
  const int32_t logicalWidth = static_cast<int32_t>(std::strlen(value)) * 6 * actualSize *
                               240 / round_ui::pins::kPanelSize;
  text(value, centerX - logicalWidth / 2, y, color, size);
}

void formatCompact(uint32_t value, char* output, size_t capacity) {
  if (value < 1000U) {
    std::snprintf(output, capacity, "%lu", static_cast<unsigned long>(value));
  } else if (value < 10000U) {
    std::snprintf(output, capacity, "%lu.%luK", static_cast<unsigned long>(value / 1000U),
                  static_cast<unsigned long>((value % 1000U) / 100U));
  } else if (value < 1000000U) {
    std::snprintf(output, capacity, "%luK", static_cast<unsigned long>(value / 1000U));
  } else if (value < 1000000000U) {
    std::snprintf(output, capacity, "%luM", static_cast<unsigned long>(value / 1000000U));
  } else {
    std::snprintf(output, capacity, "%luB", static_cast<unsigned long>(value / 1000000000U));
  }
}

void formatCountdown(uint32_t seconds, char* output, size_t capacity) {
  const uint32_t minutes = seconds / 60U;
  const uint32_t remaining = seconds % 60U;
  std::snprintf(output, capacity, "%lu:%02lu", static_cast<unsigned long>(minutes),
                static_cast<unsigned long>(remaining));
}

void drawShield(int32_t x, int32_t y, uint16_t color) {
  // A small outlined shield is cheaper than a bitmap and remains crisp on both
  // panel sizes. The clipping helpers avoid rebuilding it for unrelated rows.
  line(x, y, x + 10, y, color);
  line(x, y, x, y + 9, color);
  line(x + 10, y, x + 10, y + 9, color);
  line(x, y + 9, x + 5, y + 15, color);
  line(x + 10, y + 9, x + 5, y + 15, color);
  line(x + 3, y + 7, x + 5, y + 9, color);
  line(x + 5, y + 9, x + 8, y + 5, color);
}

void drawNetworkIcon(int32_t x, int32_t y, uint16_t color) {
  circle(x, y + 7, 2, color);
  line(x - 7, y + 3, x, y - 3, color);
  line(x, y - 3, x + 7, y + 3, color);
  line(x - 4, y + 7, x, y + 3, color);
  line(x, y + 3, x + 4, y + 7, color);
}

void drawHeader() {
  fillRect(0, 0, 240, 42, kBackground);
  // Keep the header centred: at the top of a circular panel the usable chord
  // is narrow, so a left/right toolbar would be clipped by the mask.
  drawShield(74, 9, kTeal);
  text("ADBLOCK", 93, 8, kInk, 1);

  const uint16_t statusColor = paint.portal ? kAmber : (paint.blocking ? kTeal : kCoral);
  char status[24];
  if (paint.portal) {
    std::snprintf(status, sizeof(status), "SETUP");
  } else if (paint.blocking) {
    std::snprintf(status, sizeof(status), "ACTIVE");
  } else if (paint.resumeSeconds) {
    char countdown[12];
    formatCountdown(paint.resumeSeconds, countdown, sizeof(countdown));
    std::snprintf(status, sizeof(status), "PAUSED %s", countdown);
  } else {
    std::snprintf(status, sizeof(status), "PAUSED");
  }
  fillRoundRect(66, 29, 108, 12, 6,
                paint.portal ? kPanelRaised : (paint.blocking ? kTealDim : kCoralDim));
  textCentered(status, 120, 30, statusColor, 1);
}

void drawStats() {
  fillRect(0, 42, 240, 72, kBackground);
  fillRoundRect(13, 45, 214, 65, 14, kPanel);

  char count[16];
  text("BLOCKED", 23, 51, kMuted, 1);
  formatCompact(paint.blocked, count, sizeof(count));
  text(count, 23, 63, kCoral, 2);
  text("ALLOWED", 169, 51, kMuted, 1);
  formatCompact(paint.allowed, count, sizeof(count));
  text(count, 169, 63, kTeal, 2);

  const uint8_t percent = round_ui::blockedPercent(paint.blocked, paint.allowed);
  const uint16_t rateColor = percent ? kTeal : kMuted;
  circle(120, 76, 27, kPanelRaised);
  circle(120, 76, 24, rateColor);
  circle(120, 76, 21, kPanel);
  char rate[8];
  std::snprintf(rate, sizeof(rate), "%u%%", static_cast<unsigned>(percent));
  textCentered(rate, 120, 66, kInk, 2);
  textCentered("BLOCK RATE", 120, 88, kMuted, 1);

  char domains[16];
  char custom[16];
  formatCompact(paint.domains, domains, sizeof(domains));
  formatCompact(paint.customDomains, custom, sizeof(custom));
  char summary[40];
  std::snprintf(summary, sizeof(summary), "LIST %s   CUSTOM %s", domains, custom);
  textCentered(summary, 120, 99, kInk, 1);
}

void drawNetwork() {
  fillRect(0, 114, 240, 52, kBackground);
  fillRoundRect(13, 117, 214, 45, 14, kPanel);
  drawNetworkIcon(26, 127, paint.portal ? kAmber : (paint.connected ? kTeal : kMuted));
  text("NETWORK", 40, 120, kMuted, 1);

  char clientLabel[20];
  std::snprintf(clientLabel, sizeof(clientLabel), "%u CLIENTS",
                static_cast<unsigned>(paint.clients));
  textCentered(clientLabel, 188, 120, kTeal, 1);

  if (paint.portal) {
    text("AP", 23, 135, kAmber, 1);
    text(paint.ap[0] ? paint.ap : "C3-AdBlock", 40, 135, kInk, 1);
    char lineText[32];
    std::snprintf(lineText, sizeof(lineText), "OPEN %s",
                  paint.ip[0] ? paint.ip : "192.168.4.1");
    text(lineText, 23, 149, kBlue, 1);
  } else if (paint.connected) {
    char lineText[32];
    std::snprintf(lineText, sizeof(lineText), "IP %s", paint.ip[0] ? paint.ip : "unknown");
    text(lineText, 23, 135, kInk, 1);
    std::snprintf(lineText, sizeof(lineText), "RSSI %ld dBm", static_cast<long>(paint.rssi));
    text(lineText, 23, 149, kMuted, 1);
  } else {
    text("WIFI OFFLINE", 23, 135, kMuted, 1);
    text("CHECK NETWORK", 23, 149, kMuted, 1);
  }
}

void drawButton(int32_t x, const char* title, const char* subtitle, bool selected,
                uint16_t accent) {
  fillRoundRect(x, 174, 50, 32, 8, selected ? (accent == kTeal ? kTealDim : kCoralDim)
                                             : kPanel);
  outlineRoundRect(x, 174, 50, 32, 8, selected ? accent : kPanelRaised);
  if (subtitle) {
    textCentered(title, x + 25, 178, selected ? kInk : kMuted, 1);
    textCentered(subtitle, x + 25, 191, selected ? accent : kMuted, 1);
  } else {
    textCentered(title, x + 25, 185, selected ? kInk : kMuted, 1);
  }
}

void drawControls() {
  fillRect(0, 166, 240, 74, kBackground);
  textCentered(paint.portal ? "NETWORK SETUP" : "TOUCH TO CHANGE", 120, 213,
               paint.portal ? kAmber : kMuted, 1);
  const bool active = paint.blocking && !paint.portal;
  drawButton(38, "PAUSE", "5 MIN", active, kTeal);
  drawButton(95, "PAUSE", "30 MIN", active, kTeal);
  drawButton(152, "RESUME", nullptr, !paint.blocking && !paint.portal, kTeal);
}

void drawRegion(uint8_t region) {
  switch (region) {
    case 0: drawHeader(); break;
    case 1: drawStats(); break;
    case 2: drawNetwork(); break;
    case 3: drawControls(); break;
    default: break;
  }
}

int8_t selectNextRegion() {
  for (uint8_t offset = 0; offset < kRegionCount; ++offset) {
    const uint8_t candidate = static_cast<uint8_t>((nextRegion + offset) % kRegionCount);
    if (dirtyRegions & (1U << candidate)) return static_cast<int8_t>(candidate);
  }
  return -1;
}

}  // namespace

namespace round_ui::display {

bool begin() {
  pinMode(round_ui::pins::kDisplayBacklight, OUTPUT);
  digitalWrite(round_ui::pins::kDisplayBacklight, HIGH);
  digitalWrite(round_ui::pins::kDisplayBacklight, LOW);
  if (!gfx.init()) return false;
  gfx.setRotation(0);
  gfx.setColorDepth(16);
  gfx.fillScreen(kBackground);
  digitalWrite(round_ui::pins::kDisplayBacklight, HIGH);
  current = round_ui::Snapshot{};
  paint = current;
  dirtyRegions = 0x0f;
  activeRegion = -1;
  nextRegion = 0;
  maxRenderUs = 0;
  renderCount = 0;
  return true;
}

void setSnapshot(const round_ui::Snapshot& snapshot) {
  if (sameSnapshot(current, snapshot)) return;
  uint8_t changed = 0;
  if (current.blocking != snapshot.blocking || current.resumeSeconds != snapshot.resumeSeconds ||
      current.portal != snapshot.portal)
    changed |= 0x01;
  if (round_ui::controlStateChanged(current, snapshot)) changed |= 0x08;
  if (current.blocked != snapshot.blocked || current.allowed != snapshot.allowed ||
      current.domains != snapshot.domains || current.customDomains != snapshot.customDomains)
    changed |= 0x02;
  if (current.connected != snapshot.connected || current.portal != snapshot.portal ||
      current.clients != snapshot.clients || current.rssi != snapshot.rssi ||
      std::strcmp(current.ip, snapshot.ip) || std::strcmp(current.ap, snapshot.ap))
    changed |= 0x04;
  dirtyRegions |= changed;
  current = snapshot;
  // Keep the region being painted frozen. If it changed during its stripes,
  // leave its bit dirty and let round robin repaint it with a fresh snapshot.
  if (activeRegion >= 0 && (changed & (1U << activeRegion))) activeChanged = true;
}

bool renderOneRegion(uint32_t now) {
  if (!dirtyRegions) return false;
  if (activeRegion < 0) {
    activeRegion = selectNextRegion();
    if (activeRegion < 0) return false;
    sliceY = kRegionTop[activeRegion];
    paint = current;
    activeChanged = false;
  }

  const int32_t end = kRegionTop[activeRegion + 1];
  const int32_t remaining = end - sliceY;
  const int32_t height = remaining < kSliceHeight ? remaining : kSliceHeight;
  clipTop = sliceY;
  clipBottom = sliceY + height;
  const uint32_t started = micros();
  gfx.setClipRect(0, scaled(sliceY), round_ui::pins::kPanelSize, scaled(height));
  drawRegion(static_cast<uint8_t>(activeRegion));
  gfx.clearClipRect();
  const uint32_t elapsed = micros() - started;
  if (elapsed > maxRenderUs) maxRenderUs = elapsed;
  ++renderCount;
  sliceY += height;
  if (sliceY >= end) {
    if (!activeChanged) dirtyRegions &= ~(1U << activeRegion);
    nextRegion = static_cast<uint8_t>((activeRegion + 1) % kRegionCount);
    activeRegion = -1;
  }

  // A periodic bounded diagnostic confirms the actual loop cost on hardware.
  static uint32_t lastReport = 0;
  if (now - lastReport >= 15000) {
    lastReport = now;
    Serial.printf("[round-ui] render_max_us=%lu renders=%lu heap=%lu\n",
                  static_cast<unsigned long>(maxRenderUs),
                  static_cast<unsigned long>(renderCount),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  }
  return true;
}

uint32_t maxRenderMicros() { return maxRenderUs; }

}  // namespace round_ui::display

#else

namespace round_ui::display {
bool begin() { return false; }
void setSnapshot(const Snapshot&) {}
bool renderOneRegion(uint32_t) { return false; }
uint32_t maxRenderMicros() { return 0; }
}  // namespace round_ui::display

#endif
