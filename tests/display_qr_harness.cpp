#include <cassert>
#include <cstdio>
#include <cstring>

#include "display_qr.h"

int main(int argc, char** argv) {
  using namespace round_ui;
  Snapshot snapshot;
  snapshot.connected = true;
  std::snprintf(snapshot.ip, sizeof(snapshot.ip), "%s", argc > 1 ? argv[1] : "192.168.31.107");
  assert(display_qr::prepare(snapshot));
  assert(display_qr::available());
  assert(display_qr::prepare(snapshot));
  // Finder patterns exercise high packed bits as well as low bits. The
  // encoder's C bool API is not ABI-compatible with C++ bool on every host.
  const int offsets[] = {0, 18};
  for (int offset : offsets) {
    for (int y = 0; y < 7; ++y) {
      for (int x = 0; x < 7; ++x) {
        const bool expected = x == 0 || x == 6 || y == 0 || y == 6 ||
                              (x >= 2 && x <= 4 && y >= 2 && y <= 4);
        assert(display_qr::dark(x + offset, y) == expected);
      }
    }
  }
  assert(!display_qr::dark(-1, 0));
  assert(!display_qr::dark(25, 0));
  for (int y = 0; y < display_qr::kSize; ++y) {
    for (int x = 0; x < display_qr::kSize; ++x) std::putchar(display_qr::dark(x, y) ? '1' : '0');
    std::putchar('\n');
  }
  snapshot.connected = false;
  assert(!display_qr::prepare(snapshot));
  assert(!display_qr::available());
  assert(!display_qr::dark(0, 0));
  snapshot.portal = true;
  std::strcpy(snapshot.ip, "192.168.4.1");
  assert(display_qr::prepare(snapshot));
  assert(display_qr::dark(0, 0));
}
