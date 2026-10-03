#include "display.h"

#if defined(ROUND_DISPLAY)

#include <Arduino.h>
#include <LovyanGFX.hpp>
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
round_ui::Snapshot current;
round_ui::Snapshot paint;
bool activeChanged = false;
uint8_t dirtyRegions = 0x0f;
int8_t activeRegion = -1;
int32_t sliceY = 0;
constexpr int32_t kRegionTop[] = {0, 50, 108, 163, 240};
constexpr int32_t kSliceHeight = 8;
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

constexpr uint16_t kBackground = rgb565(8, 12, 18);
constexpr uint16_t kPanel = rgb565(18, 25, 34);
constexpr uint16_t kText = rgb565(235, 242, 248);
constexpr uint16_t kMuted = rgb565(155, 170, 184);
constexpr uint16_t kGreen = rgb565(56, 211, 137);
constexpr uint16_t kRed = rgb565(245, 100, 105);
constexpr uint16_t kBlue = rgb565(89, 166, 255);

int32_t scaled(int32_t value) { return value * round_ui::pins::kPanelSize / 240; }
void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
  gfx.fillRect(scaled(x), scaled(y), scaled(w), scaled(h), color);
}
void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint16_t color) {
  gfx.fillRoundRect(scaled(x), scaled(y), scaled(w), scaled(h), scaled(radius), color);
}

void text(const char* value, int32_t x, int32_t y, uint16_t color, uint8_t size = 1) {
  gfx.setTextColor(color);
  gfx.setTextSize(size * (round_ui::pins::kPanelSize / 240.0f));
  gfx.drawString(value, scaled(x), scaled(y));
}

void drawHeader() {
  fillRect(0, 0, 240, 50, kBackground);
  text("AdBlock", 78, 18, kText, 2);
  char status[32];
  if (paint.blocking) snprintf(status, sizeof(status), "ACTIVE");
  else if (paint.resumeSeconds) snprintf(status, sizeof(status), "PAUSED %lu:%02lu",
      static_cast<unsigned long>(paint.resumeSeconds / 60),
      static_cast<unsigned long>(paint.resumeSeconds % 60));
  else snprintf(status, sizeof(status), "PAUSED");
  text(status, (240 - strlen(status) * 6) / 2, 40, paint.blocking ? kGreen : kRed);

}

void drawStats() {
  fillRect(0, 50, 240, 58, kPanel);
  char line[48];
  const uint8_t percent = round_ui::blockedPercent(paint.blocked, paint.allowed);
  snprintf(line, sizeof(line), "blocked %lu", static_cast<unsigned long>(paint.blocked));
  text(line, 24, 58, kText);
  snprintf(line, sizeof(line), "allowed %lu", static_cast<unsigned long>(paint.allowed));
  text(line, 12, 76, kMuted);
  snprintf(line, sizeof(line), "%u%%", static_cast<unsigned>(percent));
  text(line, 150, 61, percent ? kBlue : kMuted, 2);
  snprintf(line, sizeof(line), "%lu + %lu domains", static_cast<unsigned long>(paint.domains),
           static_cast<unsigned long>(paint.customDomains));
  text(line, 12, 94, kMuted);
}

void drawNetwork() {
  fillRect(0, 108, 240, 55, kBackground);
  char line[48];
  snprintf(line, sizeof(line), "clients %u   RSSI %ld", static_cast<unsigned>(paint.clients),
           static_cast<long>(paint.rssi));
  text(line, 12, 114, kText);
  if (paint.portal) {
    text("join AP:", 12, 133, kMuted);
    text(paint.ap[0] ? paint.ap : "C3-AdBlock", 70, 133, kText);
    snprintf(line, sizeof(line), "open %s", paint.ip[0] ? paint.ip : "192.168.4.1");
    text(line, 12, 148, kBlue);
  } else if (paint.connected) {
    snprintf(line, sizeof(line), "IP %s", paint.ip[0] ? paint.ip : "unknown");
    text(line, 12, 137, kText);
  } else {
    text("WiFi offline", 12, 137, kMuted);
  }
}

void drawControls() {
  fillRect(0, 163, 240, 77, kBackground);
  // At y=206 the circular mask leaves a safe chord from roughly x=37 to
  // x=203, so all three controls stay visible at the panel edge.
  fillRoundRect(38, 174, 50, 32, 7, kPanel);
  fillRoundRect(95, 174, 50, 32, 7, kPanel);
  fillRoundRect(152, 174, 50, 32, 7, kPanel);
  text("PAUSE", 48, 178, kText);
  text("5 MIN", 48, 192, kMuted);
  text("PAUSE", 105, 178, kText);
  text("30 M", 106, 192, kMuted);
  text("RESUME", 157, 185, kText);
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
  dirtyRegions = 0x0f;
  activeRegion = -1;
  maxRenderUs = 0;
  renderCount = 0;
  return true;
}

void setSnapshot(const round_ui::Snapshot& snapshot) {
  if (sameSnapshot(current, snapshot)) return;
  uint8_t changed = 0;
  if (current.blocking != snapshot.blocking || current.resumeSeconds != snapshot.resumeSeconds)
    changed |= 0x01;
  if (current.blocked != snapshot.blocked || current.allowed != snapshot.allowed ||
      current.domains != snapshot.domains || current.customDomains != snapshot.customDomains)
    changed |= 0x02;
  if (current.connected != snapshot.connected || current.portal != snapshot.portal ||
      current.clients != snapshot.clients || current.rssi != snapshot.rssi ||
      strcmp(current.ip, snapshot.ip) || strcmp(current.ap, snapshot.ap))
    changed |= 0x04;
  dirtyRegions |= changed;
  current = snapshot;
  // Finish a frozen region before repainting newer values. Restarting on
  // every counter change could starve the lower stripes under DNS load.
  if (activeRegion >= 0 && (changed & (1U << activeRegion))) activeChanged = true;
}

bool renderOneRegion(uint32_t now) {
  if (!dirtyRegions) return false;
  if (activeRegion < 0) {
    activeRegion = 0;
    while (!(dirtyRegions & (1U << activeRegion))) ++activeRegion;
    sliceY = kRegionTop[activeRegion];
    paint = current;
    activeChanged = false;
  }
  const int32_t end = kRegionTop[activeRegion + 1];
  const int32_t height = min(kSliceHeight, end - sliceY);
  const uint32_t started = micros();
  gfx.setClipRect(0, scaled(sliceY), pins::kPanelSize, scaled(height));
  drawRegion(activeRegion);
  gfx.clearClipRect();
  const uint32_t elapsed = micros() - started;
  if (elapsed > maxRenderUs) maxRenderUs = elapsed;
  ++renderCount;
  sliceY += height;
  if (sliceY >= end) {
    if (!activeChanged) dirtyRegions &= ~(1U << activeRegion);
    activeRegion = -1;
  }
  // A periodic bounded diagnostic confirms the actual loop cost on hardware.
  static uint32_t lastReport = 0;
  if (now - lastReport >= 15000) {
    lastReport = now;
    Serial.printf("[round-ui] render_max_us=%lu renders=%lu heap=%lu\n",
        static_cast<unsigned long>(maxRenderUs), static_cast<unsigned long>(renderCount),
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
