#include "display_qr.h"

#if defined(ROUND_DISPLAY)
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <lgfx/utility/lgfx_qrcode.h>

#include "wifi_qr.h"

namespace round_ui::display_qr {
namespace {
constexpr std::size_t kMatrixBytes = (kMaxSize * kMaxSize + 7) / 8;
uint8_t matrix[kMatrixBytes] = {};
QRCode code = {};
char cachedPayload[kSetupWifiPayloadCapacity] = {};
QrView cachedView = QrView::None;
int cachedSize = 0;
bool ready = false;

void clearCache() {
  std::memset(matrix, 0, sizeof(matrix));
  std::memset(&code, 0, sizeof(code));
  cachedPayload[0] = '\0';
  cachedView = QrView::None;
  cachedSize = 0;
  ready = false;
}
}

bool prepare(const Snapshot& snapshot, QrView view) {
  char payload[kSetupWifiPayloadCapacity] = {};
  int version = 0;
  int side = 0;
  bool valid = false;
  switch (view) {
    case QrView::Dashboard:
      valid = dashboardUrl(snapshot.connected, snapshot.portal, snapshot.ip,
                           payload, sizeof(payload));
      version = kVersion;
      side = kSize;
      break;
    case QrView::SetupWifi:
      valid = setupWifiPayload(snapshot.portal, snapshot.ap, payload, sizeof(payload));
      version = kSetupVersion;
      side = kMaxSize;
      break;
    case QrView::None:
    default:
      break;
  }
  if (!valid) {
    clearCache();
    return false;
  }
  if (ready && cachedView == view && cachedSize == side &&
      std::strcmp(payload, cachedPayload) == 0) {
    return true;
  }
  std::memset(matrix, 0, sizeof(matrix));
  const uint16_t required = lgfx_qrcode_getBufferSize(static_cast<uint8_t>(version));
  ready = required <= sizeof(matrix) &&
          lgfx_qrcode_initText(&code, matrix, static_cast<uint8_t>(version),
                               ECC_MEDIUM, payload) == 0;
  if (!ready) {
    clearCache();
    return false;
  }
  std::snprintf(cachedPayload, sizeof(cachedPayload), "%s", payload);
  cachedView = view;
  cachedSize = side;
  return true;
}
bool available() { return ready; }
int size() { return ready ? cachedSize : 0; }
bool dark(int x, int y) {
  if (!ready || x < 0 || y < 0 || x >= cachedSize || y >= cachedSize) return false;
  // The bundled C header defines bool as unsigned char, and getModule can
  // return 128. Normalize the packed bit here to avoid a C/C++ bool ABI mismatch.
  const unsigned offset = static_cast<unsigned>(y * cachedSize + x);
  return (matrix[offset >> 3] & (1U << (7 - (offset & 7)))) != 0;
}
}  // namespace round_ui::display_qr
#endif
