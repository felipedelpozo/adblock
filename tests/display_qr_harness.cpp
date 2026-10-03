#include <cassert>
#include <cstdio>
#include <cstring>

#include "display_qr.h"

int main(int argc, char** argv) {
  using namespace round_ui;
  Snapshot snapshot;
  const bool setup = argc > 2 && std::strcmp(argv[2], "--setup") == 0;
  if (setup) {
    snapshot.portal = true;
    std::snprintf(snapshot.ip, sizeof(snapshot.ip), "%s", "192.168.4.1");
    std::snprintf(snapshot.ap, sizeof(snapshot.ap), "%s",
                  argc > 1 ? argv[1] : "C3-AdBlock-AB12");
    assert(display_qr::prepare(snapshot, QrView::SetupWifi));
    assert(display_qr::available());
    assert(display_qr::size() == display_qr::kMaxSize);
    assert(display_qr::prepare(snapshot, QrView::SetupWifi));
    // A view switch must retain only the active matrix and return to the
    // setup matrix after switching back.
    assert(display_qr::prepare(snapshot, QrView::Dashboard));
    assert(display_qr::size() == display_qr::kSize);
    assert(display_qr::prepare(snapshot, QrView::SetupWifi));
    assert(display_qr::size() == display_qr::kMaxSize);
    const int finderOffsets[2] = {0, display_qr::kMaxSize - 7};
    for (int finderIndex = 0; finderIndex < 2; ++finderIndex) {
      const int finderOffset = finderOffsets[finderIndex];
      for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
          const bool expected = x == 0 || x == 6 || y == 0 || y == 6 ||
                                (x >= 2 && x <= 4 && y >= 2 && y <= 4);
          assert(display_qr::dark(x + finderOffset, y) == expected);
        }
      }
    }
    assert(!display_qr::dark(-1, 0));
    assert(!display_qr::dark(display_qr::kMaxSize, 0));
    for (int y = 0; y < display_qr::kMaxSize; ++y) {
      for (int x = 0; x < display_qr::kMaxSize; ++x) {
        std::putchar(display_qr::dark(x, y) ? '1' : '0');
      }
      std::putchar('\n');
    }
    return 0;
  }

  snapshot.connected = true;
  std::snprintf(snapshot.ip, sizeof(snapshot.ip), "%s", argc > 1 ? argv[1] : "192.168.31.107");
  assert(display_qr::prepare(snapshot));
  assert(display_qr::available());
  assert(display_qr::size() == display_qr::kSize);
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
