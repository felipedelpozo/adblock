#include <unity.h>

#include <string.h>

#include "pet/PetAsset.h"
#include "../../src/pet/PetAsset.cpp"

namespace {
uint8_t asset[pet_asset::kAssetBytes];

void buildAsset() {
  memset(asset, 0, sizeof(asset));
  memcpy(asset, "BPT1", 4);
  asset[4] = pet_asset::kWidth;
  asset[5] = pet_asset::kHeight;
  asset[6] = pet_asset::kStates;
  asset[7] = pet_asset::kFramesPerSecond;
  asset[8] = pet_asset::kFramesPerState;
  for (uint8_t index = 0; index < pet_asset::kPaletteEntries; ++index) {
    const uint16_t color = static_cast<uint16_t>(0x1000U + index);
    asset[16 + index * 2] = static_cast<uint8_t>(color);
    asset[17 + index * 2] = static_cast<uint8_t>(color >> 8);
  }
  // The first two pixels of frame 0 are indices 0 and 15 (high nibble first).
  asset[48] = 0x0f;
  const uint32_t checksum = pet_asset::crc32(asset + 16, sizeof(asset) - 16);
  asset[12] = static_cast<uint8_t>(checksum);
  asset[13] = static_cast<uint8_t>(checksum >> 8);
  asset[14] = static_cast<uint8_t>(checksum >> 16);
  asset[15] = static_cast<uint8_t>(checksum >> 24);
}
}  // namespace

void setUp() { buildAsset(); }
void tearDown() {}

void test_validates_fixed_bpt1_and_reads_palette_pixels() {
  TEST_ASSERT_TRUE(pet_asset::validate(asset, sizeof(asset)));
  TEST_ASSERT_EQUAL_UINT8(48, pet_asset::width(asset));
  TEST_ASSERT_EQUAL_UINT16(0x1000, pet_asset::palette(asset, 0));
  TEST_ASSERT_EQUAL_UINT16(0x100f, pet_asset::palette(asset, 15));
  TEST_ASSERT_EQUAL_UINT8(0, pet_asset::pixel(asset, 0, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(15, pet_asset::pixel(asset, 0, 1, 0));
  TEST_ASSERT_EQUAL_UINT8(0, pet_asset::pixel(asset, 0, 48, 0));
  TEST_ASSERT_NOT_NULL(pet_asset::frame(asset, 23));
  TEST_ASSERT_NULL(pet_asset::frame(asset, 24));
}

void test_rejects_wrong_length_reserved_bytes_and_crc() {
  TEST_ASSERT_FALSE(pet_asset::validate(asset, sizeof(asset) - 1));
  asset[9] = 1;
  TEST_ASSERT_FALSE(pet_asset::validate(asset, sizeof(asset)));
  buildAsset();
  asset[200] ^= 1;
  TEST_ASSERT_FALSE(pet_asset::validate(asset, sizeof(asset)));
  TEST_ASSERT_FALSE(pet_asset::validate(nullptr, 0));
}

void test_frame_index_advances_every_250ms_within_state() {
  TEST_ASSERT_EQUAL_UINT(0, pet_asset::frameIndex(0, 0));
  TEST_ASSERT_EQUAL_UINT(0, pet_asset::frameIndex(0, 249));
  TEST_ASSERT_EQUAL_UINT(1, pet_asset::frameIndex(0, 250));
  TEST_ASSERT_EQUAL_UINT(5, pet_asset::frameIndex(0, 1250));
  TEST_ASSERT_EQUAL_UINT(6, pet_asset::frameIndex(1, 0));
  TEST_ASSERT_EQUAL_UINT(23, pet_asset::frameIndex(3, 1499));
  TEST_ASSERT_EQUAL_UINT(pet_asset::kInvalidFrameIndex, pet_asset::frameIndex(4, 0));
}

void test_view_rejects_then_accepts_without_allocating() {
  pet_asset::View view;
  TEST_ASSERT_FALSE(view.parse(asset, sizeof(asset) - 1));
  TEST_ASSERT_FALSE(view.valid());
  TEST_ASSERT_TRUE(view.parse(asset, sizeof(asset)));
  TEST_ASSERT_TRUE(view.valid());
  TEST_ASSERT_EQUAL_UINT(pet_asset::kAssetBytes, view.size());
  TEST_ASSERT_EQUAL_UINT8(15, view.pixel(0, 1, 0));
}


void test_playback_holds_contracted_poses_without_crossing_states() {
  memset(asset + 48, 0, sizeof(asset) - 48);
  for (uint8_t state = 0; state < 4; ++state) {
    for (uint8_t f = 0; f < 6; ++f) {
      if (state == 3) continue;  // Fully transparent states remain safe.
      const uint8_t width = f == 4 ? 10 : 20;
      const uint8_t height = f == 0 || f == 1 ? 15 : 30;
      for (uint8_t y = f; y < f + height; ++y) {
        for (uint8_t x = 5; x < 5 + width; ++x) {
          const size_t offset = 48 + (state * 6 + f) * 1152 + (y * 48 + x) / 2;
          asset[offset] |= x % 2 ? 1 : 16;
        }
      }
    }
  }
  uint8_t map[pet_asset::kFrames];
  pet_asset::buildPlaybackMap(asset, map);
  for (uint8_t state = 0; state < 3; ++state) {
    const uint8_t expected[] = {5, 5, 2, 3, 3, 5};
    for (uint8_t f = 0; f < 6; ++f) TEST_ASSERT_EQUAL_UINT8(state * 6 + expected[f], map[state * 6 + f]);
  }
  for (uint8_t f = 18; f < 24; ++f) TEST_ASSERT_EQUAL_UINT8(f, map[f]);
  pet_asset::buildPlaybackMap(nullptr, map);
  for (uint8_t f = 0; f < 24; ++f) TEST_ASSERT_EQUAL_UINT8(f, map[f]);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_playback_holds_contracted_poses_without_crossing_states);
  RUN_TEST(test_validates_fixed_bpt1_and_reads_palette_pixels);
  RUN_TEST(test_rejects_wrong_length_reserved_bytes_and_crc);
  RUN_TEST(test_frame_index_advances_every_250ms_within_state);
  RUN_TEST(test_view_rejects_then_accepts_without_allocating);
  return UNITY_END();
}
