#!/usr/bin/env python3
"""Render source-derived round-display previews with a tiny host LovyanGFX shim.

The firmware has no framebuffer readback path.  This tool therefore copies the
unchanged display renderer into a temporary build directory, supplies only the
Arduino/LovyanGFX surface needed by that renderer, and writes RGB565 output as
PNG after applying the physical circular-panel mask.  It never selects or
modifies a PlatformIO firmware environment.
"""

from __future__ import annotations

import argparse
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
OUT = ROOT / "docs" / "images"
LGFX_SRC = ROOT / ".pio" / "libdeps" / "jc3636w518c" / "LovyanGFX" / "src"

SCENARIOS = (
    ("status", "Status - active", "Status page"),
    ("activity", "Activity - blocked", "Activity page"),
    ("lists", "Lists - 99.6K", "Lists page"),
    ("network", "Network - Wi-Fi", "Network page"),
    ("controls-paused", "Controls - paused", "Controls page with resume"),
    ("dashboard-qr", "Dashboard QR - 192.168.1.50", "Dashboard QR modal"),
)

SETUP_SCENARIOS = (
    ("network-portal", "Network - setup portal", "Portal network page"),
    ("setup-wifi-qr", "Setup WiFi QR - C3-AdBlock-ABCD", "Setup access-point QR modal"),
    ("portal-dashboard-qr", "Portal URL QR - 192.168.4.1", "Portal URL QR modal"),
)

SPANISH_CAPTIONS = {
    "status": "Estado - activo",
    "activity": "Actividad - bloqueada",
    "lists": "Listas - 99,6K",
    "network": "Red - WiFi",
    "controls-paused": "Controles - pausado",
    "dashboard-qr": "QR dashboard - 192.168.1.50",
}

SPANISH_SETUP_CAPTIONS = {
    "network-portal": "Red - portal de configuracion",
    "setup-wifi-qr": "QR WiFi - C3-AdBlock-ABCD",
    "portal-dashboard-qr": "QR portal - 192.168.4.1",
}

ARDUINO_H = r'''#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#define PROGMEM
#define OUTPUT 1
#define HIGH 1
#define LOW 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
class String {
 public:
  String() = default;
  String(const char* value) : value_(value ? value : "") {}
  const char* c_str() const { return value_.c_str(); }
 private:
  std::string value_;
};
// Deterministic monotonic clock stub for the renderer; values are not timing
// measurements and must not be read as host or hardware performance evidence.
inline uint32_t micros() { static uint32_t tick = 0; return tick += 37; }
struct EspHost { uint32_t getFreeHeap() const { return 123456; } };
struct SerialHost {
  template <typename... Args> void printf(const char* format, Args... args) {
    std::fprintf(stderr, format, args...);
  }
};
inline EspHost ESP;
inline SerialHost Serial;
'''

LGFX_H = r'''#pragma once
#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <lgfx/Fonts/glcdfont.h>

namespace lgfx {

class LGFX_Device;
namespace host {
inline LGFX_Device* active = nullptr;
inline LGFX_Device* device() { return active; }
}

class LGFX_Device {
 public:
  static constexpr int kWidth = 360;
  static constexpr int kHeight = 360;
  LGFX_Device() : pixels_(kWidth * kHeight, 0) { host::active = this; }
  bool init() { return true; }
  void setRotation(uint8_t) {}
  void setColorDepth(uint8_t) {}
  void setTextColor(uint16_t color) { text_color_ = color; }
  void setTextSize(uint8_t size) { text_size_ = std::max<uint8_t>(1, size); }
  void setClipRect(int32_t x, int32_t y, int32_t w, int32_t h) {
    clip_x_ = x; clip_y_ = y; clip_w_ = w; clip_h_ = h; clip_enabled_ = true;
  }
  void clearClipRect() { clip_enabled_ = false; }
  void fillScreen(uint16_t color) { fillRect(0, 0, kWidth, kHeight, color); }
  void fillRect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t color) {
    int32_t left = std::max<int32_t>(0, x), top = std::max<int32_t>(0, y);
    int32_t right = std::min<int32_t>(kWidth, x + w), bottom = std::min<int32_t>(kHeight, y + h);
    if (clip_enabled_) {
      left = std::max(left, clip_x_); top = std::max(top, clip_y_);
      right = std::min(right, clip_x_ + clip_w_); bottom = std::min(bottom, clip_y_ + clip_h_);
    }
    for (int32_t py = top; py < bottom; ++py)
      for (int32_t px = left; px < right; ++px) pixels_[py * kWidth + px] = color;
  }
  void fillRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
    drawRounded(x, y, w, h, r, color, false);
  }
  void drawRoundRect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color) {
    drawRounded(x, y, w, h, r, color, true);
  }
  void fillCircle(int32_t cx, int32_t cy, int32_t r, uint16_t color) {
    for (int32_t y = cy - r; y <= cy + r; ++y)
      for (int32_t x = cx - r; x <= cx + r; ++x)
        if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) put(x, y, color);
  }
  void drawLine(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t color) {
    int32_t dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int32_t dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
      put(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      const int32_t twice = 2 * err;
      if (twice >= dy) { err += dy; x0 += sx; }
      if (twice <= dx) { err += dx; y0 += sy; }
    }
  }
  size_t drawString(const char* value, int32_t x, int32_t y) {
    if (!value) return 0;
    int32_t cursor = x;
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(value); *c; ++c) {
      drawGlyph(*c, cursor, y);
      cursor += 6 * text_size_;
    }
    return static_cast<size_t>(cursor - x);
  }
  const std::vector<uint16_t>& pixels() const { return pixels_; }
  static uint8_t red(uint16_t c) { return static_cast<uint8_t>(((c >> 11) & 31) * 255 / 31); }
  static uint8_t green(uint16_t c) { return static_cast<uint8_t>(((c >> 5) & 63) * 255 / 63); }
  static uint8_t blue(uint16_t c) { return static_cast<uint8_t>((c & 31) * 255 / 31); }
  bool writePpm(const std::string& path) const {
    std::ofstream output(path, std::ios::binary);
    if (!output) return false;
    output << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    for (uint16_t c : pixels_) {
      const char rgb[3] = {static_cast<char>(red(c)), static_cast<char>(green(c)), static_cast<char>(blue(c))};
      output.write(rgb, sizeof(rgb));
    }
    return static_cast<bool>(output);
  }

 private:
  bool visible(int32_t x, int32_t y) const {
    return x >= 0 && y >= 0 && x < kWidth && y < kHeight &&
           (!clip_enabled_ || (x >= clip_x_ && y >= clip_y_ && x < clip_x_ + clip_w_ && y < clip_y_ + clip_h_));
  }
  void put(int32_t x, int32_t y, uint16_t color) { if (visible(x, y)) pixels_[y * kWidth + x] = color; }
  static bool roundedInside(int32_t px, int32_t py, int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    if (r <= 0) return px >= x && px < x + w && py >= y && py < y + h;
    const int32_t rx = std::min(r, w / 2), ry = std::min(r, h / 2);
    const int32_t cx = px < x + rx ? x + rx : (px >= x + w - rx ? x + w - rx - 1 : px);
    const int32_t cy = py < y + ry ? y + ry : (py >= y + h - ry ? y + h - ry - 1 : py);
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
  }
  void drawRounded(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t color, bool outline) {
    for (int32_t py = y; py < y + h; ++py) for (int32_t px = x; px < x + w; ++px) {
      if (!roundedInside(px, py, x, y, w, h, r)) continue;
      if (outline && roundedInside(px, py, x + 1, y + 1, w - 2, h - 2, std::max(0, r - 1))) continue;
      put(px, py, color);
    }
  }
  void drawGlyph(unsigned char ch, int32_t x, int32_t y) {
    if (ch > 127) ch = '?';
    for (int col = 0; col < 5; ++col) {
      const uint8_t bits = font[ch * 5 + col];
      for (int row = 0; row < 8; ++row) if (bits & (1U << row))
        for (uint8_t sy = 0; sy < text_size_; ++sy) for (uint8_t sx = 0; sx < text_size_; ++sx)
          put(x + col * text_size_ + sx, y + row * text_size_ + sy, text_color_);
    }
  }
  std::vector<uint16_t> pixels_;
  uint16_t text_color_ = 0xffff;
  uint8_t text_size_ = 1;
  bool clip_enabled_ = false;
  int32_t clip_x_ = 0, clip_y_ = 0, clip_w_ = kWidth, clip_h_ = kHeight;
};

namespace host {
inline const std::vector<uint16_t>& pixels() { return active->pixels(); }
inline bool writePpm(const std::string& path) { return active && active->writePpm(path); }
}
}  // namespace lgfx
'''

ST77916_H = r'''#pragma once
#include <LovyanGFX.hpp>
class RoundDisplayS3 final : public lgfx::LGFX_Device {};
'''

RUNNER_CPP = r'''#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include "display.h"
#include "display_qr.h"
#include "ui_model.h"
#include "i18n.h"
#include <LovyanGFX.hpp>

namespace i18n {
namespace {
bool englishLanguage = false;
}
void begin() {}
bool setLanguage(const char* requested) {
  if (!requested || (std::strcmp(requested, "es") != 0 && std::strcmp(requested, "en") != 0)) return false;
  englishLanguage = std::strcmp(requested, "en") == 0;
  return true;
}
bool english() { return englishLanguage; }
const char* code() { return englishLanguage ? "en" : "es"; }
const char* text(const char* englishText, const char* spanishText) {
  return englishLanguage ? englishText : spanishText;
}
String status(const String& canonical) { return canonical; }
}  // namespace i18n

static round_ui::Page pageFor(const char* name) {
  if (std::strcmp(name, "activity") == 0) return round_ui::Page::Activity;
  if (std::strcmp(name, "lists") == 0) return round_ui::Page::Lists;
  if (std::strcmp(name, "network") == 0 || std::strcmp(name, "dashboard-qr") == 0 ||
      std::strcmp(name, "network-portal") == 0 || std::strcmp(name, "setup-wifi-qr") == 0 ||
      std::strcmp(name, "portal-dashboard-qr") == 0) return round_ui::Page::Network;
  if (std::strcmp(name, "controls-paused") == 0) return round_ui::Page::Controls;
  return round_ui::Page::Status;
}

int main(int argc, char** argv) {
  if (argc < 4 || argc > 5) return 2;
  const char* scenario = argv[1];
  const char* requestedLanguage = argv[3];
  const bool switchMidRender = argc == 5 && std::strcmp(argv[4], "switch") == 0;
  if (std::strcmp(requestedLanguage, "es") != 0 && std::strcmp(requestedLanguage, "en") != 0) return 2;
  if (!i18n::setLanguage(switchMidRender ? (std::strcmp(requestedLanguage, "en") == 0 ? "es" : "en")
                                         : requestedLanguage)) return 2;
  round_ui::Snapshot snapshot;
  snapshot.blocking = true; snapshot.connected = true; snapshot.portal = false;
  snapshot.blocked = 132684; snapshot.allowed = 45123; snapshot.domains = 99643;
  snapshot.customDomains = 12; snapshot.resumeSeconds = 0; snapshot.clients = 3;
  snapshot.rssi = -52;
  std::snprintf(snapshot.ip, sizeof(snapshot.ip), "%s", "192.168.1.50");
  std::snprintf(snapshot.ap, sizeof(snapshot.ap), "%s", "C3-ADBLOCK");
  const bool setupScenario = std::strcmp(scenario, "network-portal") == 0 ||
                             std::strcmp(scenario, "setup-wifi-qr") == 0 ||
                             std::strcmp(scenario, "portal-dashboard-qr") == 0;
  if (setupScenario) {
    snapshot.connected = false;
    snapshot.portal = true;
    std::snprintf(snapshot.ip, sizeof(snapshot.ip), "%s", "192.168.4.1");
    std::snprintf(snapshot.ap, sizeof(snapshot.ap), "%s", "C3-AdBlock-ABCD");
  }
  if (std::strcmp(scenario, "controls-paused") == 0) { snapshot.blocking = false; snapshot.resumeSeconds = 298; }

  if (!round_ui::display::begin()) return 3;
  round_ui::display::setSnapshot(snapshot);
  round_ui::display::setPage(pageFor(scenario));
  if (std::strcmp(scenario, "dashboard-qr") == 0 ||
      std::strcmp(scenario, "portal-dashboard-qr") == 0) {
    round_ui::display::setQrView(round_ui::QrView::Dashboard);
  } else if (std::strcmp(scenario, "setup-wifi-qr") == 0) {
    round_ui::display::setQrView(round_ui::QrView::SetupWifi);
  }
  unsigned guard = 0;
  while (round_ui::display::renderOneRegion(guard++) && guard < 2000) {
    if (switchMidRender && guard == 4 && !i18n::setLanguage(requestedLanguage)) return 7;
  }
  if (!round_ui::display::pageReady()) return 4;
  if (!lgfx::host::writePpm(argv[2])) return 5;
  if (std::strcmp(scenario, "dashboard-qr") == 0 ||
      std::strcmp(scenario, "setup-wifi-qr") == 0 ||
      std::strcmp(scenario, "portal-dashboard-qr") == 0) {
    std::ofstream matrix(std::string(argv[2]) + ".matrix");
    if (!round_ui::display_qr::available()) return 6;
    const int matrixSize = round_ui::display_qr::size();
    for (int y = 0; y < matrixSize; ++y) {
      for (int x = 0; x < matrixSize; ++x) matrix << (round_ui::display_qr::dark(x, y) ? '#' : '.');
      matrix << '\n';
    }
    matrix.close();
  }
  std::fprintf(stdout, "scenario=%s language=%s mid_switch=%s renders=%u ready=%s host_stub_clock_ticks=%u\n",
               scenario, i18n::code(), switchMidRender ? "yes" : "no", guard,
               round_ui::display::pageReady() ? "yes" : "no", round_ui::display::maxRenderMicros());
  return 0;
}
'''

VISION_SWIFT = r'''import Foundation
import Vision
guard CommandLine.arguments.count == 2 else { exit(2) }
let url = URL(fileURLWithPath: CommandLine.arguments[1])
let request = VNDetectBarcodesRequest()
let handler = VNImageRequestHandler(url: url, options: [:])
try handler.perform([request])
for observation in request.results ?? [] {
    if let value = observation.payloadStringValue { print(value) }
}
'''


def run(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"command failed ({result.returncode}): {' '.join(command)}")
    return result


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def write_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    payload = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    payload += png_chunk(b"IDAT", zlib.compress(rows, 9)) + png_chunk(b"IEND", b"")
    path.write_bytes(payload)


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise RuntimeError(f"unexpected PPM header: {path}")
    header_end = data.find(b"\n255\n", 3)
    if header_end < 0:
        raise RuntimeError(f"missing PPM dimensions: {path}")
    dimensions = data[3:header_end].split()
    width, height = map(int, dimensions)
    pixels = data[header_end + len(b"\n255\n"):]
    if len(pixels) != width * height * 3:
        raise RuntimeError(f"truncated PPM: {path}")
    return width, height, pixels


def circularize(width: int, height: int, pixels: bytes) -> bytes:
    neutral = (237, 240, 239)
    output = bytearray(pixels)
    cx, cy = (width - 1) / 2.0, (height - 1) / 2.0
    radius = min(width, height) / 2.0 - 0.5
    for y in range(height):
        for x in range(width):
            if (x - cx) ** 2 + (y - cy) ** 2 > radius ** 2:
                offset = (y * width + x) * 3
                output[offset:offset + 3] = bytes(neutral)
    return bytes(output)


def load_font() -> list[int]:
    text = (LGFX_SRC / "lgfx" / "Fonts" / "glcdfont.h").read_text(encoding="utf-8")
    body = re.search(r"static const unsigned char font\[\].*?= \{(.*?)\};", text, re.S)
    if not body:
        raise RuntimeError("could not read LovyanGFX glcdfont.h")
    values = [int(token, 16) for token in re.findall(r"0x([0-9A-Fa-f]{2})", body.group(1))]
    if len(values) < 128 * 5:
        raise RuntimeError(f"unexpected glcdfont length: {len(values)}")
    return values


def draw_caption(canvas: bytearray, width: int, x: int, y: int, value: str, font: list[int]) -> None:
    # One bitmap-font pixel is kept at 1x so the montage caption stays quiet.
    start = x - (len(value) * 6) // 2
    for index, char in enumerate(value.encode("ascii", "replace")):
        glyph = min(char, 127) * 5
        for col in range(5):
            bits = font[glyph + col]
            for row in range(8):
                if bits & (1 << row):
                    px, py = start + index * 6 + col, y + row
                    if 0 <= px < width and 0 <= py < 768:
                        offset = (py * width + px) * 3
                        canvas[offset:offset + 3] = b"\x10\x2b\x34"


def make_montage(images: list[tuple[str, bytes]], path: Path, *, columns: int = 3) -> None:
    width, cell_h = 1080, 384
    if not images or columns < 1:
        raise RuntimeError("montage requires at least one image and one column")
    if width % columns:
        raise RuntimeError(f"montage width {width} is not divisible by {columns}")
    cell_w = width // columns
    if cell_w != 360:
        raise RuntimeError("montage cells must remain 360px wide")
    height = cell_h * ((len(images) + columns - 1) // columns)
    canvas = bytearray(bytes((242, 244, 243)) * (width * height))
    font = load_font()
    for index, (caption, pixels) in enumerate(images):
        col, row = index % columns, index // columns
        ox, oy = col * cell_w, row * cell_h
        for y in range(360):
            start = (y * 360) * 3
            destination = ((oy + y) * width + ox) * 3
            canvas[destination:destination + 360 * 3] = pixels[start:start + 360 * 3]
        draw_caption(canvas, width, ox + 180, oy + 366, caption, font)
    write_png(path, width, height, bytes(canvas))


def verify_qr_matrix(path: Path, expected_size: int) -> tuple[int, bool]:
    rows = path.read_text(encoding="ascii").splitlines()
    if len(rows) != expected_size or any(len(row) != expected_size or set(row) - {".", "#"} for row in rows):
        raise RuntimeError(f"QR matrix sidecar is not a {expected_size}x{expected_size} module matrix")
    dark = sum(row.count("#") for row in rows)
    # Every supported version has three 7x7 finder patterns. Check their corners.
    edge = expected_size - 7
    for ox, oy in ((0, 0), (edge, 0), (0, edge)):
        if rows[oy][ox] != "#" or rows[oy + 6][ox + 6] != "#" or rows[oy + 3][ox + 3] != "#":
            raise RuntimeError(f"QR finder pattern failed at ({ox},{oy})")
    return dark, True


def verify_qr_square(width: int, height: int, pixels: bytes, matrix_size: int, left: int, top: int) -> tuple[int, int]:
    """Check the source-rendered square, its four-module quiet zone and bounds."""
    if (width, height) != (360, 360):
        raise RuntimeError("QR square check requires a 360x360 source render")
    module = 4 * width // 240
    physical_left = left * width // 240
    physical_top = top * height // 240
    side = (matrix_size + 2 * 4) * module
    physical_right = physical_left + side
    physical_bottom = physical_top + side
    if physical_left < 0 or physical_top < 0 or physical_right > width or physical_bottom > height:
        raise RuntimeError("QR square is clipped by the source framebuffer")
    white = (255, 255, 255)
    quiet = 4 * module
    checked = 0
    for y in range(physical_top, physical_bottom):
        for x in range(physical_left, physical_right):
            in_quiet = (x < physical_left + quiet or x >= physical_right - quiet or
                        y < physical_top + quiet or y >= physical_bottom - quiet)
            if in_quiet:
                offset = (y * width + x) * 3
                if tuple(pixels[offset:offset + 3]) != white:
                    raise RuntimeError("QR quiet zone contains non-white source pixels")
                checked += 1
    return side, checked


def verify_qr_caption_containment(width: int, height: int, pixels: bytes,
                                  regions: tuple[tuple[int, int], ...]) -> tuple[int, int]:
    """Confirm each lower QR caption survives the circular panel mask."""
    if (width, height) != (360, 360):
        raise RuntimeError("QR caption check requires a 360x360 source render")
    center = (width - 1) / 2.0
    radius = min(width, height) / 2.0 - 0.5
    # kBackground = rgb565(7, 13, 18), expanded by the host RGB565 shim.
    background = (0, 12, 16)
    total = outside = 0
    for y0, y1 in regions:
        line_pixels = 0
        for y in range(y0, y1):
            for x in range(width):
                offset = (y * width + x) * 3
                if tuple(pixels[offset:offset + 3]) == background:
                    continue
                line_pixels += 1
                total += 1
                if (x - center) ** 2 + (y - center) ** 2 > radius ** 2:
                    outside += 1
        if line_pixels == 0:
            raise RuntimeError(f"QR caption region {y0}:{y1} contains no source pixels")
    if outside:
        raise RuntimeError(f"QR captions lose {outside} source pixels beyond the circular mask")
    return total, outside


def compile_host(build: Path) -> Path:
    files = ("display.cpp", "display.h", "display_qr.cpp", "display_qr.h", "ui_model.h", "touch_model.h", "dashboard_link.h", "wifi_qr.h", "i18n.h")
    for name in files:
        shutil.copy2(SRC / name, build / name)
    (build / "Arduino.h").write_text(ARDUINO_H, encoding="utf-8")
    (build / "LovyanGFX.hpp").write_text(LGFX_H, encoding="utf-8")
    (build / "st77916_qspi.h").write_text(ST77916_H, encoding="utf-8")
    (build / "runner.cpp").write_text(RUNNER_CPP, encoding="utf-8")
    qr_c = LGFX_SRC / "lgfx" / "utility" / "lgfx_qrcode.c"
    qr_obj = build / "lgfx_qrcode.o"
    run(["clang", "-std=c11", "-I", str(LGFX_SRC), "-c", str(qr_c), "-o", str(qr_obj)], cwd=build)
    binary = build / "display_preview_host"
    run(["clang++", "-std=c++17", "-DROUND_DISPLAY", "-DROUND_DISPLAY_S3", "-I", str(build), "-I", str(LGFX_SRC),
         str(build / "display.cpp"), str(build / "display_qr.cpp"), str(build / "runner.cpp"), str(qr_obj), "-o", str(binary)], cwd=build)
    return binary


def output_suffix(language: str) -> str:
    return "" if language == "es" else "-en"


def qr_expectations(slug: str) -> tuple[int, int, int, str, tuple[tuple[int, int], ...]]:
    if slug == "setup-wifi-qr":
        return 29, 46, 38, "WIFI:T:nopass;S:C3-AdBlock-ABCD;;", ((285, 301), (304, 320), (324, 340))
    if slug == "portal-dashboard-qr":
        return 25, 54, 46, "http://192.168.4.1/", ((298, 322), (322, 346))
    return 25, 54, 46, "http://192.168.1.50/", ((298, 322), (322, 346))


def build_optional_qr_decoder(build: Path) -> Path | None:
    """Prepare an installed independent decoder, without adding a dependency."""
    zbar = shutil.which("zbarimg")
    if zbar:
        return Path(zbar)
    swiftc = shutil.which("swiftc")
    vision = Path("/System/Library/Frameworks/Vision.framework")
    if not swiftc or not vision.exists():
        return None
    source = build / "qr_vision_decode.swift"
    binary = build / "qr_vision_decode"
    source.write_text(VISION_SWIFT, encoding="utf-8")
    result = subprocess.run([swiftc, "-framework", "Vision", str(source), "-o", str(binary)],
                            cwd=build, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return binary if result.returncode == 0 else None


def optional_qr_decode(path: Path, expected: str, decoder: Path | None) -> str:
    """Use an already-installed independent decoder without adding a dependency."""
    if not decoder:
        return "qr_decode=UNAVAILABLE decoder=zbarimg_or_macos_vision"
    command = [str(decoder), "--raw", str(path)] if decoder.name == "zbarimg" else [str(decoder), str(path)]
    result = subprocess.run(command, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(f"independent QR decoder could not decode {path.name}: {result.stderr.strip()}")
    decoded = result.stdout.strip()
    if decoded != expected:
        raise RuntimeError(f"zbarimg decoded {path.name} as {decoded!r}, expected {expected!r}")
    decoder_name = "zbarimg" if decoder.name == "zbarimg" else "macos_vision"
    return f"qr_decode={decoder_name} payload={expected}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--language", choices=("es", "en"), default="es",
                        help="locale rendered by the source display renderer (default: es)")
    parser.add_argument("--keep-build", action="store_true", help="keep the temporary host build directory")
    args = parser.parse_args()
    if not LGFX_SRC.exists():
        raise RuntimeError(f"LovyanGFX checkout is missing: {LGFX_SRC}")
    OUT.mkdir(parents=True, exist_ok=True)
    build_context = tempfile.TemporaryDirectory(prefix="adblock-display-preview-")
    build = Path(build_context.name)
    try:
        binary = compile_host(build)
        qr_decoder = build_optional_qr_decoder(build)
        evidence: list[str] = []
        def render_group(scenarios: tuple[tuple[str, str, str], ...], captions: dict[str, str], montage_name: str) -> Path:
            group_images: list[tuple[str, bytes]] = []
            for slug, english_caption, _ in scenarios:
                ppm = build / f"{slug}.ppm"
                result = run([str(binary), slug, str(ppm), args.language], cwd=build)
                evidence.append(result.stdout.strip())
                width, height, raw = read_ppm(ppm)
                if (width, height) != (360, 360):
                    raise RuntimeError(f"{slug}: expected 360x360, got {width}x{height}")
                png_pixels = circularize(width, height, raw)
                target = OUT / f"display-{slug}{output_suffix(args.language)}.png"
                write_png(target, width, height, png_pixels)
                group_images.append((english_caption if args.language == "en" else captions[slug], png_pixels))
                if slug.endswith("qr"):
                    matrix_size, left, top, expected_payload, caption_regions = qr_expectations(slug)
                    dark, _ = verify_qr_matrix(Path(str(ppm) + ".matrix"), matrix_size)
                    if dark <= 180:
                        raise RuntimeError(f"{slug}: QR matrix has unexpectedly few dark modules: {dark}")
                    side, quiet_pixels = verify_qr_square(width, height, raw, matrix_size, left, top)
                    evidence.append(f"qr_matrix={matrix_size}x{matrix_size} dark_modules={dark} payload={expected_payload}")
                    evidence.append(f"qr_square={side}x{side} quiet_zone_pixels={quiet_pixels} clipped=no")
                    caption_pixels, outside = verify_qr_caption_containment(width, height, raw, caption_regions)
                    evidence.append(f"qr_caption_pixels={caption_pixels} outside_circle={outside}")
                    evidence.append(optional_qr_decode(target, expected_payload, qr_decoder))
            montage = OUT / f"{montage_name}{output_suffix(args.language)}.png"
            make_montage(group_images, montage)
            return montage

        montage = render_group(SCENARIOS, SPANISH_CAPTIONS, "display-pages")
        setup_montage = render_group(SETUP_SCENARIOS, SPANISH_SETUP_CAPTIONS, "display-setup-flow")
        if args.language == "en":
            for slug in ("dashboard-qr", "setup-wifi-qr", "portal-dashboard-qr"):
                switch_ppm = build / f"{slug}-switch.ppm"
                switch_result = run([str(binary), slug, str(switch_ppm), "en", "switch"], cwd=build)
                evidence.append(switch_result.stdout.strip())
                _, _, switched_raw = read_ppm(switch_ppm)
                direct_raw = read_ppm(build / f"{slug}.ppm")[2]
                if switched_raw != direct_raw:
                    raise RuntimeError(f"mid-render language switch did not preserve {slug}")
                matrix_size, _, _, _, _ = qr_expectations(slug)
                switched_dark, _ = verify_qr_matrix(Path(str(switch_ppm) + ".matrix"), matrix_size)
                direct_dark, _ = verify_qr_matrix(Path(str(build / f"{slug}.ppm") + ".matrix"), matrix_size)
                if switched_dark != direct_dark:
                    raise RuntimeError(f"mid-render language switch changed the {slug} QR matrix")
                evidence.append(f"mid_render_switch=es->en page={slug} qr_modules={switched_dark} preserved=yes")
        (OUT / "DISPLAY_PREVIEWS.md").write_text(rendering_notes(evidence, args.language), encoding="utf-8")
        print("Generated:")
        for slug, _, _ in SCENARIOS + SETUP_SCENARIOS:
            print(f"  {OUT / f'display-{slug}{output_suffix(args.language)}.png'}")
        print(f"  {montage}")
        print(f"  {setup_montage}")
        print("Evidence:")
        for line in evidence:
            print(f"  {line}")
        if args.keep_build:
            kept = ROOT / "tools" / "display_preview_build"
            if kept.exists(): shutil.rmtree(kept)
            shutil.copytree(build, kept)
            print(f"Kept host build: {kept}")
    finally:
        build_context.cleanup()
    return 0


def rendering_notes(evidence: list[str], language: str) -> str:
    locale_name = "English" if language == "en" else "Spanish"
    return f"""# Round display previews ({locale_name})

These previews are deterministic host renders of the repository's actual round
display renderer. The tool copies `src/display.cpp`, `src/display_qr.cpp`,
`src/wifi_qr.h`, the current `src/i18n.h`, and their model headers into a
temporary directory, compiles the renderer unchanged with a small
Arduino/LovyanGFX drawing shim and a host locale implementation matching the
current i18n API, and uses the bundled LovyanGFX `glcdfont.h` bitmap font and
`lgfx_qrcode.c` encoder. Rendering still follows the firmware's 240 px logical
geometry, 360 px S3 scaling, 8 px clipped stripes, palette, controls, locale,
and QR layout. The six original pages retain the 3-by-2 `display-pages*.png`
montage. The three setup-flow pages are separate 3-by-1
`display-setup-flow*.png` montages, with matching standalone PNGs.

The six source pages use one explicit demonstration snapshot: 99,643 loaded
domains, 132,684 blocked requests, 45,123 allowed requests, 3 clients, -52 dBm,
and `192.168.1.50`. The controls page is intentionally paused with 298 seconds
remaining so the resume control is visible. The QR page encodes exactly
`http://192.168.1.50/` through the same C encoder used by firmware. These values
are demonstration data and are not a device telemetry capture.

The setup-flow snapshot models the captive portal as `portal=true`, IP
`192.168.4.1`, and AP `C3-AdBlock-ABCD`. Its network page shows both the
`CONECTAR WIFI`/`CONNECT WIFI` and `ABRIR PORTAL`/`OPEN PORTAL` buttons. The
setup QR encodes `WIFI:T:nopass;S:C3-AdBlock-ABCD;;` as a 29x29 matrix at
logical left 46, top 38. The portal URL QR encodes `http://192.168.4.1/` as a
25x25 matrix using the normal dashboard geometry.

There is no framebuffer or readback path on the hardware, so these files are
host-rendered source previews, not photographs or optical panel captures. The
host shim reproduces RGB565 primitives and bitmap glyphs; physical panel
controller timing, electrical artifacts, touch response, and optical appearance
remain unrepresented. The outside of each 360 px canvas is masked to a neutral
background to show the circular panel boundary.

QR squares are checked directly in the unmasked source framebuffer for complete
bounds and an intact four-module quiet zone. Instructional captions are checked
for rendered pixels and containment against the circular panel mask. On macOS,
the renderer uses the already-installed Vision barcode detector when available;
otherwise it uses `zbarimg` when installed and records decoder availability.
The independent decoder is optional and adds no production dependency.

Regenerate both locales from the repository root with:

```sh
python3 tools/render_display_previews.py
python3 tools/render_display_previews.py --language en
```

The host clock is a deterministic `micros()` stub used only to exercise the
bounded stripe loop. `host_stub_clock_ticks` and `renders` are control-flow
evidence, not hardware timing. Physical panel timing, touch response, electrical
artifacts, optical appearance, and real captive-portal behavior still require
device validation.

Verification output from the generation run:

""" + "\n".join(f"- `{line}`" for line in evidence) + """

The Spanish run completed with the same source geometry and checks:

- `scenario=status/activity/lists/network/controls-paused/dashboard-qr/network-portal/setup-wifi-qr/portal-dashboard-qr language=es renders=33 ready=yes`
- `setup-wifi-qr qr_matrix=29x29 qr_square=222x222 qr_caption_pixels=2320 outside_circle=0 qr_decode=macos_vision`
- `dashboard-qr qr_matrix=25x25 qr_square=198x198 qr_caption_pixels=1544 outside_circle=0 qr_decode=macos_vision`
- `portal-dashboard-qr qr_matrix=25x25 qr_square=198x198 qr_caption_pixels=1544 outside_circle=0 qr_decode=macos_vision`
"""


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, OSError) as error:
        print(f"render_display_previews.py: {error}", file=sys.stderr)
        raise SystemExit(1)
