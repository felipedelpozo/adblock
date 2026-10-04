#pragma once

#include <stddef.h>
#include <stdint.h>

// Runtime appearance state is deliberately independent from PetEngine.  The
// latter owns progression and persistence of pet metrics; this namespace owns
// only the selected visual name/skin and the optional local BPT1 sprite.
namespace pet_appearance {

// Call after LittleFS mount and blocklistManager.begin(), which creates the
// shared filesystem mutex used when restoring the selected sprite.
bool begin();

const char* name();
const char* skin();
uint32_t revision();
bool customAvailable();
bool storageReady();
size_t assetBytes();
const char* assetMemory();
inline size_t assetSize() { return assetBytes(); }

// Returns the validated custom BPT1 bytes even when a built-in skin is
// selected, so the dashboard can offer an existing custom asset for download.
const uint8_t* assetData();

// Rendering must use this pointer only when it is non-null. Built-in skins
// continue to be drawn by the existing renderer, therefore sprite() is null
// unless the custom skin is selected.
const uint8_t* sprite();
size_t spriteFrame(uint8_t state, uint32_t animationMs);

// A short, stable diagnostic for the last rejected operation. Empty means no
// error has been recorded since begin().
const char* error();

bool set(const char* name, const char* skin);
inline bool select(const char* selectedSkin) { return set(name(), selectedSkin); }

bool beginUpload();
bool writeUpload(const uint8_t* bytes, size_t size);
bool finishUpload();
void abortUpload();

}  // namespace pet_appearance
