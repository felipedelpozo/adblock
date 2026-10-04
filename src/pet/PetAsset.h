#pragma once

#include <stddef.h>
#include <stdint.h>

// The BPT1 asset is intentionally a small, fixed-format value.  The parser
// never allocates and only retains a pointer owned by its caller.
namespace pet_asset {

static constexpr size_t kHeaderBytes = 16;
static constexpr size_t kPaletteEntries = 16;
static constexpr size_t kPaletteBytes = kPaletteEntries * 2;
static constexpr uint8_t kWidth = 48;
static constexpr uint8_t kHeight = 48;
static constexpr uint8_t kStates = 4;
static constexpr uint8_t kFramesPerState = 6;
static constexpr uint8_t kFrames = kStates * kFramesPerState;
static constexpr size_t kFrameBytes = (static_cast<size_t>(kWidth) * kHeight) / 2;
static constexpr size_t kAssetBytes = kHeaderBytes + kPaletteBytes + kFrames * kFrameBytes;
static constexpr uint8_t kFramesPerSecond = 4;
static constexpr uint32_t kFrameDurationMs = 250;
static constexpr size_t kInvalidFrameIndex = static_cast<size_t>(-1);
static constexpr size_t kSize = kAssetBytes;
static constexpr size_t kFrameSize = kFrameBytes;
static constexpr size_t kPaletteSize = kPaletteBytes;
static constexpr uint8_t kFrameCount = kFrames;
static constexpr uint8_t kFps = kFramesPerSecond;
static constexpr uint8_t kTransparentIndex = 0;

// Returns zero for invalid inputs.  Call validate() before using the data in
// an application-facing path; these helpers remain defensive for web/API
// callers and native tests.
uint32_t crc32(const uint8_t* data, size_t length);
bool validate(const uint8_t* data, size_t length);
const char* validationError(const uint8_t* data, size_t length);

uint8_t width(const uint8_t* data);
uint8_t height(const uint8_t* data);
uint8_t states(const uint8_t* data);
uint8_t framesPerState(const uint8_t* data);

// Palette index zero is the transparent entry.  Colors are returned in the
// file's little-endian RGB565 representation.
uint16_t palette(const uint8_t* data, uint8_t index);
inline uint16_t rgb565(const uint8_t* data, uint8_t index) {
  return palette(data, index);
}
uint8_t pixel(const uint8_t* data, size_t frame, uint8_t x, uint8_t y);
const uint8_t* frame(const uint8_t* data, size_t frame);

// State is idle, reaction, feeding, or sleeping (0..3). The six animation
// frames in a state advance every 250 ms and wrap independently.
size_t frameIndex(uint8_t state, uint32_t animationMs);
// Build once after validation. Contracted poses repeat the preceding stable
// pose in the same state, preserving timing without resizing the artwork.
void buildPlaybackMap(const uint8_t* data, uint8_t (&map)[kFrames]);
inline size_t frameIndexAt(uint32_t animationMs, uint8_t state) {
  return frameIndex(state, animationMs);
}

class View {
 public:
  View() : data_(nullptr), valid_(false), error_("asset has not been parsed") {}
  bool parse(const uint8_t* data, size_t length);
  bool valid() const { return valid_; }
  const char* error() const;
  const uint8_t* data() const { return valid_ ? data_ : nullptr; }
  size_t size() const { return valid_ ? kAssetBytes : 0; }
  uint8_t width() const { return pet_asset::width(data_); }
  uint8_t height() const { return pet_asset::height(data_); }
  uint16_t palette(uint8_t index) const { return pet_asset::palette(data_, index); }
  uint8_t pixel(size_t frameNumber, uint8_t x, uint8_t y) const {
    return pet_asset::pixel(data_, frameNumber, x, y);
  }
  const uint8_t* frame(size_t frameNumber) const {
    return pet_asset::frame(data_, frameNumber);
  }
  size_t frameIndex(uint8_t state, uint32_t animationMs) const {
    return valid_ ? pet_asset::frameIndex(state, animationMs) : kInvalidFrameIndex;
  }

 private:
  const uint8_t* data_;
  bool valid_;
  const char* error_;
};

// A descriptive alias makes the parser convenient at call sites without
// introducing a second implementation or a platform dependency.
using PetAsset = View;

}  // namespace pet_asset
