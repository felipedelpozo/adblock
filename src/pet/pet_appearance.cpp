#include "pet_appearance.h"

#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#if defined(ESP32)
#include "esp_heap_caps.h"
#endif

#include "PetAsset.h"
#include "../blocklist_manager.h"

namespace pet_appearance {
namespace {

constexpr char kNamespace[] = "pet-look";
constexpr char kAssetSlotA[] = "/pet-sprite-a.bpt";
constexpr char kAssetSlotB[] = "/pet-sprite-b.bpt";
constexpr char kTempPath[] = "/pet-sprite.tmp";
constexpr char kSelectionKey[] = "selection";
constexpr uint32_t kSelectionMagic = 0x314B4C50UL;  // "PLK1" little endian.
constexpr uint8_t kSelectionVersion = 2;
constexpr size_t kNameCapacity = 17;  // 1..16 bytes plus NUL.
constexpr size_t kSkinCapacity = 8;   // "classic", "amber", "violet", "custom".
constexpr size_t kMinimumHeapHeadroom = 32768;

struct __attribute__((packed)) SelectionRecord {
  uint32_t magic;
  uint8_t version;
  uint8_t slot;
  uint8_t reserved[2];
  uint32_t revision;
  char name[kNameCapacity];
  char skin[kSkinCapacity];
  uint32_t crc;
};

static_assert(sizeof(SelectionRecord) == 41, "selection record must remain fixed-size");

Preferences preferences;
bool started = false;
bool nvsReady = false;
bool uploadActive = false;
bool uploadFailed = false;
File uploadFile;
size_t uploadSize = 0;

char selectedName[kNameCapacity] = "Adagotchi";
char selectedSkin[kSkinCapacity] = "classic";
uint32_t selectedRevision = 0;

uint8_t* customBytes = nullptr;
uint8_t playbackMap[pet_asset::kFrames] = {};
bool customBytesPsram = false;
uint8_t selectedSlot = 0;
char lastError[128] = {};

void setError(const char* value) {
  if (!value) value = "unknown appearance error";
  strncpy(lastError, value, sizeof(lastError) - 1);
  lastError[sizeof(lastError) - 1] = '\0';
}

void clearError() { lastError[0] = '\0'; }

void releaseBytes(uint8_t* bytes, bool psram) {
  if (!bytes) return;
#if defined(ESP32)
  if (psram) {
    heap_caps_free(bytes);
    return;
  }
#else
  (void)psram;
#endif
  free(bytes);
}

uint8_t* allocateBytes(bool& psram) {
  psram = false;
#if defined(ESP32) && defined(BOARD_HAS_PSRAM)
  if (psramFound()) {
    void* value = heap_caps_malloc(pet_asset::kAssetBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (value) {
      psram = true;
      return static_cast<uint8_t*>(value);
    }
  }
#endif
#if defined(ESP32)
  if (ESP.getMaxAllocHeap() < pet_asset::kAssetBytes + kMinimumHeapHeadroom) return nullptr;
#endif
  return static_cast<uint8_t*>(malloc(pet_asset::kAssetBytes));
}

bool validName(const char* input, char output[kNameCapacity]) {
  if (!input || !output) return false;
  size_t length = 0;
  while (input[length] != '\0') {
    if (++length > 64) return false;
  }
  size_t first = 0;
  size_t last = length;
  while (first < last && input[first] == ' ') ++first;
  while (last > first && input[last - 1] == ' ') --last;
  if (first == last || last - first > kNameCapacity - 1) return false;

  size_t out = 0;
  for (size_t i = first; i < last; ++i) {
    const unsigned char ch = static_cast<unsigned char>(input[i]);
    const bool letter = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
    const bool digit = ch >= '0' && ch <= '9';
    if (!letter && !digit && ch != ' ' && ch != '_' && ch != '-') return false;
    output[out++] = static_cast<char>(ch);
  }
  output[out] = '\0';
  return out != 0;
}

bool validSkin(const char* value) {
  return value && (!strcmp(value, "classic") || !strcmp(value, "amber") ||
                   !strcmp(value, "violet") || !strcmp(value, "custom"));
}

uint32_t recordCrc(const SelectionRecord& record) {
  return pet_asset::crc32(reinterpret_cast<const uint8_t*>(&record),
                          offsetof(SelectionRecord, crc));
}

bool validRecord(const SelectionRecord& record) {
  return record.magic == kSelectionMagic && record.version == kSelectionVersion &&
         record.slot < 2 && record.reserved[0] == 0 && record.reserved[1] == 0 &&
         record.name[kNameCapacity - 1] == '\0' && record.skin[kSkinCapacity - 1] == '\0' &&
         validSkin(record.skin) && recordCrc(record) == record.crc;
}

SelectionRecord makeRecord(const char* candidateName, const char* candidateSkin,
                           uint32_t revision, uint8_t slot) {
  SelectionRecord record = {};
  record.magic = kSelectionMagic;
  record.version = kSelectionVersion;
  record.slot = slot;
  record.revision = revision;
  strncpy(record.name, candidateName, kNameCapacity - 1);
  strncpy(record.skin, candidateSkin, kSkinCapacity - 1);
  record.crc = recordCrc(record);
  return record;
}

bool persist(const char* candidateName, const char* candidateSkin, uint32_t revision,
             uint8_t slot) {
  if (!nvsReady) {
    setError("appearance NVS is unavailable");
    return false;
  }
  const SelectionRecord record = makeRecord(candidateName, candidateSkin, revision, slot);
  if (preferences.putBytes(kSelectionKey, &record, sizeof(record)) != sizeof(record)) {
    setError("appearance selection could not be committed");
    return false;
  }
  return true;
}

bool loadSelection() {
  const size_t length = preferences.getBytesLength(kSelectionKey);
  if (!length) return true;
  SelectionRecord record = {};
  if (preferences.getBytesLength(kSelectionKey) != sizeof(record) ||
      preferences.getBytes(kSelectionKey, &record, sizeof(record)) != sizeof(record) ||
      !validRecord(record) || !validName(record.name, selectedName)) {
    strcpy(selectedName, "Adagotchi");
    strcpy(selectedSkin, "classic");
    selectedRevision = 0;
    selectedSlot = 0;
    setError("invalid or newer appearance record preserved; persistence disabled");
    return false;
  }
  strcpy(selectedSkin, record.skin);
  selectedRevision = record.revision;
  selectedSlot = record.slot;
  // Keep a persisted custom selection even when its file is temporarily
  // unavailable; sprite() remains null until a validated file is loaded.
  return true;
}

bool readAssetLocked(const char* path, uint8_t*& bytes, bool& psram) {
  bytes = nullptr;
  psram = false;
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  if (file.size() != pet_asset::kAssetBytes) {
    file.close();
    setError("custom sprite has an invalid size");
    return false;
  }
  uint8_t* candidate = allocateBytes(psram);
  if (!candidate) {
    file.close();
    setError("not enough RAM for custom sprite");
    return false;
  }
  size_t offset = 0;
  while (offset < pet_asset::kAssetBytes) {
    const size_t read = file.read(candidate + offset, pet_asset::kAssetBytes - offset);
    if (read == 0) break;
    offset += read;
  }
  file.close();
  if (offset != pet_asset::kAssetBytes || !pet_asset::validate(candidate, offset)) {
    const char* reason = offset != pet_asset::kAssetBytes
                             ? "custom sprite read was truncated"
                             : pet_asset::validationError(candidate, offset);
    releaseBytes(candidate, psram);
    setError(reason);
    return false;
  }
  bytes = candidate;
  return true;
}

bool loadAsset() {
  BlocklistFilesystemGuard guard(1000);
  if (!guard) {
    setError("appearance filesystem is busy");
    return false;
  }
  uint8_t* candidate = nullptr;
  bool candidatePsram = false;
  const char* path = selectedSlot == 0 ? kAssetSlotA : kAssetSlotB;
  if (!readAssetLocked(path, candidate, candidatePsram)) return false;
  uint8_t* old = customBytes;
  const bool oldPsram = customBytesPsram;
  customBytes = candidate;
  pet_asset::buildPlaybackMap(customBytes, playbackMap);
  customBytesPsram = candidatePsram;
  releaseBytes(old, oldPsram);
  return true;
}

bool finishFailed(const char* message) {
  if (message) setError(message);
  BlocklistFilesystemGuard guard(1000);
  if (guard) LittleFS.remove(kTempPath);
  uploadActive = false;
  uploadFailed = false;
  uploadSize = 0;
  return false;
}

}  // namespace

bool begin() {
  if (started) return nvsReady;
  started = true;
  clearError();
  nvsReady = preferences.begin(kNamespace, false);
  if (!nvsReady) {
    setError("appearance NVS is unavailable");
    return false;
  }
  if (!loadSelection()) { nvsReady = false; return false; }
  // The preference layer remains usable when no custom file exists. A bad
  // file is retained for diagnostics and never replaces a previously loaded
  // in-memory asset.
  loadAsset();
  return true;
}

const char* name() { return selectedName; }
const char* skin() { return selectedSkin; }
uint32_t revision() { return selectedRevision; }
bool customAvailable() { return customBytes != nullptr; }
bool storageReady() { return nvsReady; }
size_t assetBytes() { return customBytes ? pet_asset::kAssetBytes : 0; }
const char* assetMemory() { return !customBytes ? "none" : customBytesPsram ? "psram" : "heap"; }
const uint8_t* assetData() { return customBytes; }
const uint8_t* sprite() {
  return customBytes && !strcmp(selectedSkin, "custom") ? customBytes : nullptr;
}
size_t spriteFrame(uint8_t state, uint32_t animationMs) {
  const size_t index = pet_asset::frameIndex(state, animationMs);
  return customBytes && index < pet_asset::kFrames ? playbackMap[index] : pet_asset::kInvalidFrameIndex;
}
const char* error() { return lastError; }

bool set(const char* candidateName, const char* candidateSkin) {
  clearError();
  char normalizedName[kNameCapacity] = {};
  if (!validName(candidateName, normalizedName)) {
    setError("pet name must be 1-16 ASCII letters, digits, spaces, _ or -");
    return false;
  }
  if (!validSkin(candidateSkin)) {
    setError("skin must be classic, amber, violet, or custom");
    return false;
  }
  if (!strcmp(candidateSkin, "custom") && !customBytes) {
    setError("custom skin is unavailable");
    return false;
  }
  if (!strcmp(normalizedName, selectedName) && !strcmp(candidateSkin, selectedSkin)) return true;
  const uint32_t next = selectedRevision == UINT32_MAX ? 1 : selectedRevision + 1;
  if (!persist(normalizedName, candidateSkin, next, selectedSlot)) return false;
  strcpy(selectedName, normalizedName);
  strcpy(selectedSkin, candidateSkin);
  selectedRevision = next;
  return true;
}

bool beginUpload() {
  clearError();
  if (!nvsReady) {
    setError("appearance storage is not ready");
    return false;
  }
  if (uploadActive) {
    setError("custom sprite upload is already active");
    return false;
  }
  BlocklistFilesystemGuard guard(1000);
  if (!guard) {
    setError("appearance filesystem is busy");
    return false;
  }
  LittleFS.remove(kTempPath);
  uploadFile = LittleFS.open(kTempPath, "w");
  if (!uploadFile) {
    setError("custom sprite staging file could not be opened");
    return false;
  }
  uploadActive = true;
  uploadFailed = false;
  uploadSize = 0;
  return true;
}

bool writeUpload(const uint8_t* bytes, size_t size) {
  if (!uploadActive || uploadFailed || !bytes || size == 0 ||
      size > pet_asset::kAssetBytes - uploadSize) {
    setError("custom sprite upload exceeds 27696 bytes");
    uploadFailed = true;
    return false;
  }
  BlocklistFilesystemGuard guard(1000);
  if (!guard) {
    setError("appearance filesystem is busy");
    uploadFailed = true;
    return false;
  }
  const size_t written = uploadFile.write(bytes, size);
  if (written != size) {
    setError("custom sprite staging write failed");
    uploadFailed = true;
    return false;
  }
  uploadSize += written;
  return true;
}

bool finishUpload() {
  if (!uploadActive) {
    setError("custom sprite upload is not active");
    return false;
  }
  if (uploadFile) {
    BlocklistFilesystemGuard guard(1000);
    if (!guard) return finishFailed("appearance filesystem is busy");
    uploadFile.flush();
    uploadFile.close();
  }
  if (uploadFailed) return finishFailed(nullptr);
  if (uploadSize != pet_asset::kAssetBytes) {
    return finishFailed("custom sprite upload is incomplete");
  }

  BlocklistFilesystemGuard guard(1000);
  if (!guard) return finishFailed("appearance filesystem is busy");
  uint8_t* candidate = nullptr;
  bool candidatePsram = false;
  if (!readAssetLocked(kTempPath, candidate, candidatePsram)) {
    LittleFS.remove(kTempPath);
    uploadActive = false;
    uploadSize = 0;
    return false;
  }
  const uint32_t next = selectedRevision == UINT32_MAX ? 1 : selectedRevision + 1;
  const uint8_t nextSlot = selectedSlot == 0 ? 1 : 0;
  const char* nextPath = nextSlot == 0 ? kAssetSlotA : kAssetSlotB;
  // The old slot remains active until the single NVS selection record points
  // at the validated replacement. A power loss leaves either complete slot
  // available, and startup always follows the previously committed record.
  LittleFS.remove(nextPath);
  if (!LittleFS.rename(kTempPath, nextPath)) {
    releaseBytes(candidate, candidatePsram);
    uploadActive = false;
    uploadSize = 0;
    setError("custom sprite atomic rename failed; previous sprite retained");
    return false;
  }
  uint8_t* old = customBytes;
  const bool oldPsram = customBytesPsram;
  if (!persist(selectedName, "custom", next, nextSlot)) {
    // The active slot and NVS selection are unchanged. The inactive file can
    // be left as harmless garbage if removal itself is interrupted.
    LittleFS.remove(nextPath);
    releaseBytes(candidate, candidatePsram);
    uploadActive = false;
    uploadSize = 0;
    return false;
  }
  customBytes = candidate;
  pet_asset::buildPlaybackMap(customBytes, playbackMap);
  customBytesPsram = candidatePsram;
  strcpy(selectedSkin, "custom");
  selectedRevision = next;
  selectedSlot = nextSlot;
  uploadActive = false;
  uploadSize = 0;
  releaseBytes(old, oldPsram);
  clearError();
  return true;
}

void abortUpload() {
  if (uploadFile) {
    BlocklistFilesystemGuard guard(1000);
    if (guard) {
      uploadFile.close();
      LittleFS.remove(kTempPath);
    }
  }
  uploadActive = false;
  uploadFailed = false;
  uploadSize = 0;
}

}  // namespace pet_appearance
