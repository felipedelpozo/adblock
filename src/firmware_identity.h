#pragma once

#include <Arduino.h>
#include <string.h>

#ifndef ADBLOCK_FW_VERSION
#define ADBLOCK_FW_VERSION "0.3.2"
#endif

namespace firmware_identity {

// These identifiers are part of the release contract. A firmware built for a
// display profile must never be offered to a different board profile.
#if defined(ROUND_DISPLAY_S3)
static constexpr const char* PROFILE = "jc3636w518c";
#define ADBLOCK_PROFILE_LITERAL "jc3636w518c"
static constexpr const char* CHIP = "ESP32-S3";
static constexpr const char* BOARD = "jc3636w518c";
#elif defined(ADBLOCK_PROFILE_S3_HEADLESS) || (defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(ROUND_DISPLAY_S3))
static constexpr const char* PROFILE = "s3-headless";
#define ADBLOCK_PROFILE_LITERAL "s3-headless"
static constexpr const char* CHIP = "ESP32-S3";
static constexpr const char* BOARD = "jc3636w518c";
#elif defined(ROUND_DISPLAY)
static constexpr const char* PROFILE = "round-display";
#define ADBLOCK_PROFILE_LITERAL "round-display"
static constexpr const char* CHIP = "ESP32-C3";
static constexpr const char* BOARD = "esp32-c3-devkitm-1";
#else
static constexpr const char* PROFILE = "c3";
#define ADBLOCK_PROFILE_LITERAL "c3"
static constexpr const char* CHIP = "ESP32-C3";
static constexpr const char* BOARD = "esp32-c3-devkitm-1";
#endif

static constexpr const char* VERSION = ADBLOCK_FW_VERSION;
static constexpr const char* MARKER = "ADBLOCK_ID:" ADBLOCK_PROFILE_LITERAL ":" ADBLOCK_FW_VERSION;

inline bool runtimeCompatible() {
  const String model = ESP.getChipModel();
  if (model != CHIP) return false;
  // The S3 board has a 16 MB flash contract. The C3 profiles share the
  // Arduino board definition, so the profile/board identity is compile-time.
  if (strcmp(PROFILE, "jc3636w518c") == 0 || strcmp(PROFILE, "s3-headless") == 0)
    return ESP.getFlashChipSize() >= 16UL * 1024UL * 1024UL;
  return ESP.getFlashChipSize() >= 4UL * 1024UL * 1024UL;
}

}  // namespace firmware_identity
