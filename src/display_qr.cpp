#include "display_qr.h"

#if defined(ROUND_DISPLAY)
#include <cstdio>
#include <cstring>
#include <lgfx/utility/lgfx_qrcode.h>

namespace round_ui::display_qr {
namespace {
uint8_t matrix[(kSize * kSize + 7) / 8] = {};
QRCode code = {};
char cachedUrl[24] = {};
bool ready = false;
}

bool prepare(const Snapshot& snapshot) {
  char url[24];
  if (!dashboardUrl(snapshot.connected, snapshot.portal, snapshot.ip, url, sizeof(url))) {
    ready = false;
    cachedUrl[0] = '\0';
    return false;
  }
  if (ready && std::strcmp(url, cachedUrl) == 0) return true;
  ready = lgfx_qrcode_getBufferSize(kVersion) <= sizeof(matrix) &&
          lgfx_qrcode_initText(&code, matrix, kVersion, ECC_MEDIUM, url) == 0;
  if (ready) std::snprintf(cachedUrl, sizeof(cachedUrl), "%s", url);
  return ready;
}
bool available() { return ready; }
bool dark(int x, int y) {
  if (!ready || x < 0 || y < 0 || x >= kSize || y >= kSize) return false;
  // The bundled C header defines bool as unsigned char, and getModule can
  // return 128. Normalize the packed bit here to avoid a C/C++ bool ABI mismatch.
  const unsigned offset = y * kSize + x;
  return (matrix[offset >> 3] & (1U << (7 - (offset & 7)))) != 0;
}
}  // namespace round_ui::display_qr
#endif
