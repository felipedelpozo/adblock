#include "PetAsset.h"

#include <string.h>

namespace pet_asset {
namespace {

uint16_t read16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         static_cast<uint16_t>(data[offset + 1]) << 8;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t length) {
  if (!data && length != 0) return 0;
  uint32_t crc = 0xffffffffUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

const char* validationError(const uint8_t* data, size_t length) {
  if (!data) return "asset data is null";
  if (length != kAssetBytes) return "asset length is not 27696 bytes";
  if (memcmp(data, "BPT1", 4) != 0) return "asset magic is not BPT1";
  if (data[4] != kWidth || data[5] != kHeight || data[6] != kStates ||
      data[7] != kFramesPerSecond || data[8] != kFramesPerState) {
    return "asset metadata is not 48x48, 4 states, 4 fps, 6 frames";
  }
  if (data[9] != 0 || data[10] != 0 || data[11] != 0) {
    return "asset reserved header bytes are non-zero";
  }
  const uint32_t expected = static_cast<uint32_t>(data[12]) |
                            static_cast<uint32_t>(data[13]) << 8 |
                            static_cast<uint32_t>(data[14]) << 16 |
                            static_cast<uint32_t>(data[15]) << 24;
  if (crc32(data + kHeaderBytes, kAssetBytes - kHeaderBytes) != expected) {
    return "asset CRC32 does not match";
  }
  return nullptr;
}

bool validate(const uint8_t* data, size_t length) {
  return validationError(data, length) == nullptr;
}

uint8_t width(const uint8_t* data) {
  return data ? data[4] : 0;
}

uint8_t height(const uint8_t* data) {
  return data ? data[5] : 0;
}

uint8_t states(const uint8_t* data) {
  return data ? data[6] : 0;
}

uint8_t framesPerState(const uint8_t* data) {
  return data ? data[8] : 0;
}

uint16_t palette(const uint8_t* data, uint8_t index) {
  if (!data || index >= kPaletteEntries) return 0;
  return read16(data, kHeaderBytes + static_cast<size_t>(index) * 2);
}

const uint8_t* frame(const uint8_t* data, size_t frameNumber) {
  if (!data || frameNumber >= kFrames) return nullptr;
  return data + kHeaderBytes + kPaletteBytes + frameNumber * kFrameBytes;
}

uint8_t pixel(const uint8_t* data, size_t frameNumber, uint8_t x, uint8_t y) {
  if (!data || frameNumber >= kFrames || x >= kWidth || y >= kHeight) return 0;
  const size_t offset = kHeaderBytes + kPaletteBytes + frameNumber * kFrameBytes +
                        (static_cast<size_t>(y) * kWidth + x) / 2;
  const uint8_t packed = data[offset];
  return (x & 1U) == 0 ? static_cast<uint8_t>(packed >> 4)
                        : static_cast<uint8_t>(packed & 0x0fU);
}

void buildPlaybackMap(const uint8_t* data, uint8_t (&map)[kFrames]) {
  for (uint8_t state = 0; state < kStates; ++state) {
    uint8_t widths[kFramesPerState] = {}, heights[kFramesPerState] = {};
    uint8_t reference = 0;
    for (uint8_t f = 0; f < kFramesPerState; ++f) {
      uint8_t minX = kWidth, minY = kHeight, maxX = 0, maxY = 0;
      for (uint8_t y = 0; y < kHeight; ++y) {
        for (uint8_t x = 0; x < kWidth; ++x) {
          if (!pixel(data, state * kFramesPerState + f, x, y)) continue;
          if (x < minX) minX = x;
          if (x > maxX) maxX = x;
          if (y < minY) minY = y;
          if (y > maxY) maxY = y;
        }
      }
      if (minX != kWidth) {
        widths[f] = maxX - minX + 1;
        heights[f] = maxY - minY + 1;
      }
      if (widths[f] * heights[f] > widths[reference] * heights[reference]) reference = f;
    }
    const auto stable = [&](uint8_t f) {
      return widths[f] * 100 >= widths[reference] * 90 &&
             heights[f] * 100 >= heights[reference] * 90;
    };
    for (uint8_t f = 0; f < kFramesPerState; ++f) {
      uint8_t selected = f;
      while (!stable(selected)) selected = (selected + kFramesPerState - 1) % kFramesPerState;
      map[state * kFramesPerState + f] = state * kFramesPerState + selected;
    }
  }
}

size_t frameIndex(uint8_t state, uint32_t animationMs) {
  if (state >= kStates) return kInvalidFrameIndex;
  const size_t inState = (animationMs / kFrameDurationMs) % kFramesPerState;
  return static_cast<size_t>(state) * kFramesPerState + inState;
}

bool View::parse(const uint8_t* data, size_t length) {
  data_ = nullptr;
  valid_ = false;
  error_ = validationError(data, length);
  if (error_) return false;
  data_ = data;
  valid_ = true;
  error_ = nullptr;
  return true;
}

const char* View::error() const {
  return valid_ ? nullptr : error_;
}

}  // namespace pet_asset
