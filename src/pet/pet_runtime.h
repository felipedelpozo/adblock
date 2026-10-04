#pragma once

#include "PetEngine.h"

// The engine is owned by the main loop. Only serialized copies cross into
// the low-priority persistence worker; DNS handling never writes flash.
namespace pet_runtime {
pet::PetEngine& engine();
bool begin(uint32_t now);
void service(uint32_t now);
bool checkpoint(uint32_t timeoutMs = 2000);
// Safe from the OTA worker: waits for already submitted copies only.
bool flushPending(uint32_t timeoutMs = 2000);
bool storageReady();
bool savePending();
uint32_t maxSaveMicros();
}
