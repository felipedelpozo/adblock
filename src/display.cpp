#include "display.h"

#if defined(ROUND_DISPLAY)

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <cstdio>
#include <cstring>
#include "display_qr.h"

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

// All geometry is authored at 240x240. The S3 driver scales each write to its
// native 360x360 panel, keeping touch coordinates and the C3 renderer aligned.
constexpr uint8_t kRegionCount = 4;
constexpr uint8_t kAllRegions = (1U << kRegionCount) - 1U;
constexpr int32_t kRegionTop[kRegionCount + 1] = {0, 60, 120, 180, 240};
constexpr int32_t kSliceHeight = 8;

round_ui::Snapshot current;
round_ui::Snapshot paint;
round_ui::Page currentPage = round_ui::Page::Status;
round_ui::Page paintPage = round_ui::Page::Status;
bool currentQr = false;
bool paintQr = false;
bool activeChanged = false;
bool pageReadyFlag = false;
uint8_t dirtyRegions = kAllRegions;
uint8_t initialPendingRegions = kAllRegions;
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

// The palette follows the approved dark display fragment: ink on a near-black
// surface, teal for healthy state, amber for setup and coral for a paused state.
constexpr uint16_t kBackground = rgb565(7, 13, 18);
constexpr uint16_t kPanel = rgb565(15, 27, 35);
constexpr uint16_t kPanelRaised = rgb565(22, 40, 47);
constexpr uint16_t kInk = rgb565(232, 242, 242);
constexpr uint16_t kMuted = rgb565(143, 168, 169);
constexpr uint16_t kTeal = rgb565(45, 214, 180);
constexpr uint16_t kTealDim = rgb565(24, 91, 86);
constexpr uint16_t kCoral = rgb565(244, 111, 113);
constexpr uint16_t kAmber = rgb565(248, 190, 75);

int32_t scaled(int32_t value) {
  return value * round_ui::pins::kPanelSize / 240;
}

bool intersects(int32_t y, int32_t height) {
  return y < clipBottom && y + height > clipTop;
}

void fillRect(int32_t x, int32_t y, int32_t width, int32_t height, uint16_t color) {
  gfx.fillRect(scaled(x), scaled(y), scaled(width), scaled(height), color);
}

void fillRoundRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t radius,
                   uint16_t color) {
  if (!intersects(y, height)) return;
  gfx.fillRoundRect(scaled(x), scaled(y), scaled(width), scaled(height), scaled(radius), color);
}

void outlineRoundRect(int32_t x, int32_t y, int32_t width, int32_t height, int32_t radius,
                      uint16_t color) {
  if (!intersects(y, height)) return;
  gfx.drawRoundRect(scaled(x), scaled(y), scaled(width), scaled(height), scaled(radius), color);
}

void fillCircle(int32_t x, int32_t y, int32_t radius, uint16_t color) {
  if (!intersects(y - radius, radius * 2 + 1)) return;
  gfx.fillCircle(scaled(x), scaled(y), scaled(radius), color);
}

void line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) {
  const int32_t top = y1 < y2 ? y1 : y2;
  const int32_t bottom = y1 > y2 ? y1 : y2;
  if (!intersects(top, bottom - top + 1)) return;
  gfx.drawLine(scaled(x1), scaled(y1), scaled(x2), scaled(y2), color);
}

uint8_t physicalTextSize(uint8_t logicalSize) {
  // LovyanGFX bitmap fonts use integer sizes. The extra step on 360px makes
  // the small Spanish labels readable while retaining logical geometry.
  if (round_ui::pins::kPanelSize >= 360 && logicalSize > 0) return logicalSize + 1;
  return logicalSize;
}

int32_t textHeight(uint8_t logicalSize) {
  const uint8_t actualSize = physicalTextSize(logicalSize);
  return (8 * actualSize * 240 + round_ui::pins::kPanelSize - 1) /
             round_ui::pins::kPanelSize +
         1;
}

void text(const char* value, int32_t x, int32_t y, uint16_t color, uint8_t size = 1) {
  if (!value || !intersects(y, textHeight(size))) return;
  gfx.setTextColor(color);
  gfx.setTextSize(physicalTextSize(size));
  gfx.drawString(value, scaled(x), scaled(y));
}

void textCentered(const char* value, int32_t centerX, int32_t y, uint16_t color,
                 uint8_t size = 1) {
  if (!value) return;
  const uint8_t actualSize = physicalTextSize(size);
  const int32_t logicalWidth = static_cast<int32_t>(std::strlen(value)) * 6 * actualSize * 240 /
                               round_ui::pins::kPanelSize;
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

void formatCount(uint32_t value, char* output, size_t capacity) {
  // Keep six-digit counts exact (the approved preview shows 132.684) and use
  // compact notation once the count would no longer fit a round display.
  if (value < 1000U) {
    std::snprintf(output, capacity, "%lu", static_cast<unsigned long>(value));
  } else if (value < 1000000U) {
    std::snprintf(output, capacity, "%lu.%03lu", static_cast<unsigned long>(value / 1000U),
                  static_cast<unsigned long>(value % 1000U));
  } else {
    formatCompact(value, output, capacity);
  }
}

void formatCountdown(uint32_t seconds, char* output, size_t capacity) {
  const uint32_t minutes = seconds / 60U;
  const uint32_t remaining = seconds % 60U;
  std::snprintf(output, capacity, "%lu:%02lu", static_cast<unsigned long>(minutes),
                static_cast<unsigned long>(remaining));
}

void formatPercentTenths(uint16_t tenths, char* output, size_t capacity) {
  const uint16_t bounded = tenths > 1000U ? 1000U : tenths;
  std::snprintf(output, capacity, "%u,%u%%", static_cast<unsigned>(bounded / 10U),
                static_cast<unsigned>(bounded % 10U));
}

void drawShield(int32_t x, int32_t y, uint16_t color) {
  line(x, y, x + 20, y, color);
  line(x, y, x, y + 18, color);
  line(x + 20, y, x + 20, y + 18, color);
  line(x, y + 18, x + 10, y + 30, color);
  line(x + 20, y + 18, x + 10, y + 30, color);
  line(x + 6, y + 14, x + 10, y + 18, color);
  line(x + 10, y + 18, x + 16, y + 10, color);
}

void drawListIcon(int32_t x, int32_t y, uint16_t color) {
  for (int32_t row = 0; row < 3; ++row) {
    const int32_t lineY = y + row * 6;
    fillCircle(x, lineY, 1, color);
    line(x + 5, lineY, x + 18, lineY, color);
  }
}

void drawNetworkIcon(int32_t x, int32_t y, uint16_t color) {
  fillCircle(x, y + 7, 2, color);
  line(x - 8, y + 2, x, y - 5, color);
  line(x, y - 5, x + 8, y + 2, color);
  line(x - 5, y + 7, x, y + 3, color);
  line(x, y + 3, x + 5, y + 7, color);
}

void drawPageTitle(const char* title) {
  textCentered(title, 120, 22, kMuted, 1);
}

void drawDots() {
  constexpr int32_t kFirstDot = 104;
  for (uint8_t index = 0; index < 5; ++index) {
    const bool selected =
        (index == 0 && currentPage == round_ui::Page::Status) ||
        (index == 1 && currentPage == round_ui::Page::Activity) ||
        (index == 2 && currentPage == round_ui::Page::Lists) ||
        (index == 3 && currentPage == round_ui::Page::Network) ||
        (index == 4 && currentPage == round_ui::Page::Controls);
    fillCircle(kFirstDot + index * 8, 214, selected ? 2 : 1, selected ? kTeal : kMuted);
  }
}

void drawStatusPage() {
  drawPageTitle("ESTADO");
  const bool paused = !paint.blocking && !paint.portal;
  const uint16_t accent = paint.portal ? kAmber : (paused ? kCoral : kTeal);
  drawShield(110, 63, accent);

  if (paint.portal) {
    textCentered("CONFIGURA WIFI", 120, 106, kAmber, 2);
    textCentered("PORTAL ACTIVO", 120, 146, kMuted, 1);
  } else {
    textCentered(paused ? "PAUSADO" : "ACTIVO", 120, 106, accent, 4);
    textCentered(paused ? "BLOQUEO EN PAUSA" : "PROTECCION ACTIVA", 120, 146, kMuted, 1);
    if (paused) {
      if (paint.resumeSeconds == 0) {
        textCentered("PAUSA SIN LIMITE", 120, 173, kInk, 1);
      } else {
        char countdown[12];
        formatCountdown(paint.resumeSeconds, countdown, sizeof(countdown));
        char detail[24];
        std::snprintf(detail, sizeof(detail), "REANUDA EN %s", countdown);
        textCentered(detail, 120, 173, kInk, 1);
      }
    } else if (!paint.connected) {
      textCentered("SIN CONEXION WIFI", 120, 173, kCoral, 1);
    }
  }
}

void drawActivityPage() {
  drawPageTitle("ACTIVIDAD");
  const uint16_t tenths = round_ui::blockedPercentTenths(paint.blocked, paint.allowed);
  char percentage[12];
  formatPercentTenths(tenths, percentage, sizeof(percentage));
  // At 100%, the extra digit needs a smaller C3 font to stay inside the circle.
  const uint8_t rateSize = round_ui::pins::kPanelSize >= 360 ? 7 : (tenths == 1000 ? 5 : 6);
  textCentered(percentage, 120, 65, kInk, rateSize);
  textCentered("CONSULTAS BLOQUEADAS", 120, 120, kMuted, 1);

  char blocked[16];
  char allowed[16];
  formatCount(paint.blocked, blocked, sizeof(blocked));
  formatCount(paint.allowed, allowed, sizeof(allowed));
  textCentered(blocked, 75, 145, kCoral, 2);
  textCentered(allowed, 165, 145, kTeal, 2);
  textCentered("BLOQUEADAS", 75, 173, kMuted, 1);
  textCentered("PERMITIDAS", 165, 173, kMuted, 1);
  textCentered("DESDE EL ARRANQUE", 120, 197, kMuted, 1);
}

void drawListsPage() {
  drawPageTitle("LISTAS");
  drawListIcon(111, 63, kMuted);
  char loaded[16];
  formatCount(paint.domains, loaded, sizeof(loaded));
  textCentered(loaded, 120, 100, kInk, 4);
  textCentered("DOMINIOS CARGADOS", 120, 142, kMuted, 1);

  char custom[16];
  formatCount(paint.customDomains, custom, sizeof(custom));
  char detail[32];
  std::snprintf(detail, sizeof(detail), "%s PERSONALIZADOS", custom);
  textCentered(detail, 120, 176, kMuted, 1);
}

void drawControlButton(int32_t x, int32_t y, int32_t width, int32_t height, const char* label,
                       bool selected, bool enabled, uint16_t accent);

void drawDashboardButton() {
  const auto& bounds = round_ui::kDashboardButton;
  drawControlButton(bounds.x, bounds.y, bounds.width, bounds.height,
                    "ABRIR DASHBOARD", false, round_ui::dashboardAvailable(paint), kTeal);
}

void drawNetworkPage() {
  drawPageTitle("RED");
  const uint16_t accent = paint.portal ? kAmber : (paint.connected ? kTeal : kCoral);
  drawNetworkIcon(120, 50, accent);

  if (paint.portal) {
    textCentered("PORTAL WIFI", 120, 78, kAmber, 1);
    char portalIp[24];
    std::snprintf(portalIp, sizeof(portalIp), "%s", paint.ip[0] ? paint.ip : "192.168.4.1");
    textCentered(portalIp, 120, 101, kInk, 2);
    textCentered("AP", 120, 139, kAmber, 1);
    textCentered(paint.ap[0] ? paint.ap : "C3-ADBLOCK", 120, 150, kInk, 1);
    drawDashboardButton();
    return;
  } else if (paint.connected) {
    textCentered("WIFI CONECTADO", 120, 78, kTeal, 1);
    textCentered(paint.ip[0] ? paint.ip : "SIN IP", 120, 101, kInk, 2);
  } else {
    textCentered("SIN CONEXION", 120, 78, kCoral, 1);
    textCentered("SIN IP", 120, 101, kMuted, 2);
  }

  char clients[16];
  std::snprintf(clients, sizeof(clients), "%u", static_cast<unsigned>(paint.clients));
  textCentered(clients, 72, 137, paint.connected ? kInk : kMuted, 2);
  textCentered("CLIENTES DNS", 72, 164, kMuted, 1);

  char rssi[20];
  if (paint.connected) {
    std::snprintf(rssi, sizeof(rssi), "%ld", static_cast<long>(paint.rssi));
    textCentered(rssi, 168, 137, kInk, 2);
    textCentered("SENAL dBm", 168, 164, kMuted, 1);
  } else {
    textCentered("--", 168, 137, kMuted, 2);
    textCentered("SENAL dBm", 168, 164, kMuted, 1);
  }
  drawDashboardButton();
}

void drawDashboardQr() {
  drawPageTitle(paint.portal ? "PORTAL WIFI" : "DASHBOARD");
  if (!round_ui::display_qr::available()) {
    textCentered("QR NO DISPONIBLE", 120, 106, kMuted, 1);
  } else {
    using namespace round_ui::display_qr;
    constexpr int side = (kSize + 2 * kQuietZone) * kModulePixels;
    // The four-module quiet zone is white even on the dark display theme.
    fillRect(kLeft, kTop, side, side, 0xffff);
    for (int y = 0; y < kSize; ++y) {
      const int top = kTop + (y + kQuietZone) * kModulePixels;
      if (!intersects(top, kModulePixels)) continue;
      for (int x = 0; x < kSize; ++x) {
        if (dark(x, y)) fillRect(kLeft + (x + kQuietZone) * kModulePixels,
                                top, kModulePixels, kModulePixels, 0x0000);
      }
    }
  }
  textCentered(paint.ip, 120, 183, kInk, 1);
  textCentered("CONECTA A LA MISMA WIFI", 120, 199, kMuted, 1);
  textCentered("TOCA PARA VOLVER", 120, 215, kMuted, 1);
}

void drawControlButton(int32_t x, int32_t y, int32_t width, int32_t height, const char* label,
                       bool selected, bool enabled, uint16_t accent) {
  const uint16_t fill = !enabled ? kPanel : (selected ? kTealDim : kPanel);
  const uint16_t border = !enabled ? kPanelRaised : (selected ? accent : kPanelRaised);
  const uint16_t ink = !enabled ? kMuted : kInk;
  fillRoundRect(x, y, width, height, 8, fill);
  outlineRoundRect(x, y, width, height, 8, border);
  textCentered(label, x + width / 2, y + (height - textHeight(1)) / 2, ink, 1);
}

void drawControlsPage() {
  drawPageTitle("CONTROLES");
  const bool paused = !paint.blocking && !paint.portal;
  const bool enabled = !paint.portal;
  if (paint.portal) {
    textCentered("CONFIGURA WIFI", 120, 55, kAmber, 1);
  } else if (paused) {
    if (paint.resumeSeconds == 0) {
      textCentered("PAUSADO", 120, 55, kCoral, 1);
    } else {
      char countdown[12];
      formatCountdown(paint.resumeSeconds, countdown, sizeof(countdown));
      char state[24];
      std::snprintf(state, sizeof(state), "PAUSADO %s", countdown);
      textCentered(state, 120, 55, kCoral, 1);
    }
  } else {
    textCentered("PROTECCION ACTIVA", 120, 55, kTeal, 1);
  }

  // These bounds come from the shared touch contract. Keeping the renderer
  // tied to the same constants prevents visual and touch geometry drift.
  drawControlButton(round_ui::kPause5Button.x, round_ui::kPause5Button.y,
                    round_ui::kPause5Button.width, round_ui::kPause5Button.height,
                    "PAUSAR 5 MIN", false, enabled, kTeal);
  drawControlButton(round_ui::kPause30Button.x, round_ui::kPause30Button.y,
                    round_ui::kPause30Button.width, round_ui::kPause30Button.height,
                    "PAUSAR 30 MIN", false, enabled, kTeal);
  if (paused) {
    drawControlButton(round_ui::kResumeButton.x, round_ui::kResumeButton.y,
                      round_ui::kResumeButton.width, round_ui::kResumeButton.height,
                      "REANUDAR", true, true, kTeal);
  } else {
    // Clear the conditional control in case the previous frozen snapshot was
    // paused. The current stripe clip makes this a local, incremental erase.
    fillRoundRect(round_ui::kResumeButton.x, round_ui::kResumeButton.y,
                  round_ui::kResumeButton.width, round_ui::kResumeButton.height, 8,
                  kBackground);
    textCentered(paint.portal ? "ABRE EL PORTAL" : "REANUDA AUTOMATICAMENTE", 120, 182, kMuted, 1);
  }
}

void drawCurrentPage() {
  fillRect(0, 0, 240, 240, kBackground);
  if (paintQr) {
    drawDashboardQr();
    return;
  }
  switch (paintPage) {
    case round_ui::Page::Status: drawStatusPage(); break;
    case round_ui::Page::Activity: drawActivityPage(); break;
    case round_ui::Page::Lists: drawListsPage(); break;
    case round_ui::Page::Network: drawNetworkPage(); break;
    case round_ui::Page::Controls: drawControlsPage(); break;
  }
  drawDots();
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
  currentPage = round_ui::Page::Status;
  paintPage = currentPage;
  currentQr = paintQr = false;
  pageReadyFlag = false;
  dirtyRegions = kAllRegions;
  initialPendingRegions = kAllRegions;
  activeRegion = -1;
  nextRegion = 0;
  maxRenderUs = 0;
  renderCount = 0;
  return true;
}

void setPage(round_ui::Page page) {
  if (currentPage == page) return;
  currentPage = page;
  // Cancelling the active region is essential: one loop iteration may be
  // painting a stripe from the old page when a swipe arrives.
  activeRegion = -1;
  activeChanged = false;
  nextRegion = 0;
  sliceY = 0;
  dirtyRegions = kAllRegions;
  initialPendingRegions = kAllRegions;
  pageReadyFlag = false;
}

bool pageReady() { return pageReadyFlag; }

void setDashboardQr(bool visible) {
  visible = visible && currentPage == round_ui::Page::Network;
  if (currentQr == visible) return;
  currentQr = visible;
  if (visible) display_qr::prepare(current);
  activeRegion = -1;
  activeChanged = false;
  nextRegion = 0;
  sliceY = 0;
  dirtyRegions = kAllRegions;
  initialPendingRegions = kAllRegions;
  pageReadyFlag = false;
}

void setSnapshot(const round_ui::Snapshot& snapshot) {
  if (sameSnapshot(current, snapshot)) return;

  const bool blockingChanged = current.blocking != snapshot.blocking;
  const bool portalChanged = current.portal != snapshot.portal;
  const bool connectedChanged = current.connected != snapshot.connected;
  const bool countersChanged = current.blocked != snapshot.blocked ||
                               current.allowed != snapshot.allowed;
  const bool domainsChanged = current.domains != snapshot.domains;
  const bool customDomainsChanged = current.customDomains != snapshot.customDomains;
  const bool countdownChanged = current.resumeSeconds != snapshot.resumeSeconds;
  const bool networkIdentityChanged = connectedChanged || portalChanged ||
                                      std::strcmp(current.ip, snapshot.ip) != 0 ||
                                      std::strcmp(current.ap, snapshot.ap) != 0;
  const bool networkStatsChanged = current.clients != snapshot.clients ||
                                   current.rssi != snapshot.rssi;

  current = snapshot;

  if (currentQr) {
    if (networkIdentityChanged) {
      display_qr::prepare(current);
      activeRegion = -1;
      nextRegion = 0;
      dirtyRegions = initialPendingRegions = kAllRegions;
      pageReadyFlag = false;
    }
    return;
  }

  // Only repaint visible content. Hidden-page fields are retained in the
  // latest snapshot and will be shown when setPage() invalidates everything.
  uint8_t changedRegions = 0;
  switch (currentPage) {
    case round_ui::Page::Status:
      if (blockingChanged || portalChanged) changedRegions |= kAllRegions;
      if (countdownChanged || connectedChanged) changedRegions |= 0x0c;
      break;
    case round_ui::Page::Activity:
      if (countersChanged) changedRegions |= 0x06;
      break;
    case round_ui::Page::Lists:
      if (domainsChanged) changedRegions |= 0x06;
      if (customDomainsChanged) changedRegions |= 0x0c;
      break;
    case round_ui::Page::Network:
      if (networkIdentityChanged) changedRegions |= kAllRegions;
      if (networkStatsChanged) changedRegions |= 0x04;
      break;
    case round_ui::Page::Controls:
      if (blockingChanged || portalChanged) changedRegions |= kAllRegions;
      if (countdownChanged) changedRegions |= 0x03;
      break;
  }
  dirtyRegions |= changedRegions;
  // Freeze the snapshot for the current region. It will be repainted from the
  // new snapshot on the next round, avoiding mixed data within one stripe set.
  if (activeRegion >= 0 && changedRegions & (1U << activeRegion)) activeChanged = true;
}

bool renderOneRegion(uint32_t now) {
  if (!dirtyRegions) return false;
  if (activeRegion < 0) {
    activeRegion = selectNextRegion();
    if (activeRegion < 0) return false;
    sliceY = kRegionTop[activeRegion];
    paint = current;
    paintPage = currentPage;
    paintQr = currentQr;
    activeChanged = false;
  }

  const int32_t end = kRegionTop[activeRegion + 1];
  const int32_t remaining = end - sliceY;
  const int32_t height = remaining < kSliceHeight ? remaining : kSliceHeight;
  clipTop = sliceY;
  clipBottom = sliceY + height;
  const uint32_t started = micros();
  gfx.setClipRect(0, scaled(sliceY), round_ui::pins::kPanelSize, scaled(height));
  drawCurrentPage();
  gfx.clearClipRect();
  const uint32_t elapsed = micros() - started;
  if (elapsed > maxRenderUs) maxRenderUs = elapsed;
  ++renderCount;
  sliceY += height;
  if (sliceY >= end) {
    initialPendingRegions &= static_cast<uint8_t>(~(1U << activeRegion));
    if (!activeChanged) dirtyRegions &= ~(1U << activeRegion);
    nextRegion = static_cast<uint8_t>((activeRegion + 1) % kRegionCount);
    activeRegion = -1;
    if (initialPendingRegions == 0) pageReadyFlag = true;
  }

  // A periodic bounded diagnostic confirms the actual loop cost on hardware.
  static uint32_t lastReport = 0;
  if (now - lastReport >= 15000) {
    lastReport = now;
    Serial.printf("[round-ui] render_max_us=%lu renders=%lu heap=%lu page_ready=%s\n",
                  static_cast<unsigned long>(maxRenderUs),
                  static_cast<unsigned long>(renderCount),
                  static_cast<unsigned long>(ESP.getFreeHeap()), pageReadyFlag ? "yes" : "no");
  }
  return true;
}

uint32_t maxRenderMicros() { return maxRenderUs; }

}  // namespace round_ui::display

#else

namespace round_ui::display {
bool begin() { return false; }
void setPage(round_ui::Page) {}
void setDashboardQr(bool) {}
bool pageReady() { return false; }
void setSnapshot(const Snapshot&) {}
bool renderOneRegion(uint32_t) { return false; }
uint32_t maxRenderMicros() { return 0; }
}  // namespace round_ui::display

#endif
