#include "pet_runtime.h"

#include <Arduino.h>
#include <Preferences.h>
#include <atomic>

namespace {
pet::PetEngine petEngine;
Preferences preferences;
QueueHandle_t saveQueue = nullptr;
std::atomic<bool> ready{false};
std::atomic<uint32_t> requested{0}, committed{0}, maxWriteUs{0};
uint32_t nextSequence = 0, lastCheckpoint = 0, lastRewards = 0;
constexpr uint32_t kCheckpointMs = 15UL * 60UL * 1000UL;
struct Image {
  uint32_t sequence;
  uint8_t bytes[pet::PetEngine::kEncodedSize];
};
Image outgoing;

void saveWorker(void*) {
  Image incoming;
  for (;;) {
    if (xQueueReceive(saveQueue, &incoming, portMAX_DELAY) != pdTRUE) continue;
    const uint32_t start = micros();
    // NVS commits a single blob atomically and handles power-loss recovery.
    // It is independent of the filesystem lock used by the DNS blocklist.
    const bool saved = preferences.putBytes("state", incoming.bytes,
                                            sizeof(incoming.bytes)) == sizeof(incoming.bytes);
    const uint32_t elapsed = micros() - start;
    if (elapsed > maxWriteUs.load()) maxWriteUs = elapsed;
    if (saved) {
      committed = incoming.sequence;
    } else {
      Serial.println("[pet] NVS checkpoint failed; retaining previous committed state");
      // Keep the latest snapshot available for retry without overwriting a
      // newer copy that the main loop has already submitted.
      if (uxQueueMessagesWaiting(saveQueue) == 0) xQueueSend(saveQueue, &incoming, 0);
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

bool submit(uint32_t now) {
  if (!ready.load() || !saveQueue) return false;
  petEngine.tick(now);
  if (!petEngine.encode(outgoing.bytes, sizeof(outgoing.bytes))) return false;
  outgoing.sequence = ++nextSequence;
  requested = outgoing.sequence;
  if (xQueueOverwrite(saveQueue, &outgoing) != pdTRUE) return false;
  lastCheckpoint = now;
  lastRewards = petEngine.snapshot().rewardEvents;
  return true;
}
}

namespace pet_runtime {
pet::PetEngine& engine() { return petEngine; }

bool begin(uint32_t now) {
  petEngine.tick(now);
  if (!preferences.begin("adagotchi", false)) {
    Serial.println("[pet] NVS unavailable; progress is volatile");
    return false;
  }
  const size_t size = preferences.getBytesLength("state");
  if (size) {
    if (size != sizeof(outgoing.bytes) ||
        preferences.getBytes("state", outgoing.bytes, sizeof(outgoing.bytes)) != size ||
        !petEngine.decode(outgoing.bytes, size, now)) {
      // A corrupt or newer-version record must not be silently replaced.
      Serial.println("[pet] invalid NVS state preserved; persistence disabled");
      return false;
    }
  }
  saveQueue = xQueueCreate(1, sizeof(Image));
  TaskHandle_t worker = nullptr;
  if (!saveQueue || xTaskCreate(saveWorker, "pet-save", 6144, nullptr, 1, &worker) != pdPASS) {
    if (saveQueue) vQueueDelete(saveQueue);
    saveQueue = nullptr;
    Serial.println("[pet] persistence worker unavailable; progress is volatile");
    return false;
  }
  ready = true;
  lastCheckpoint = now;
  lastRewards = petEngine.snapshot().rewardEvents;
  const auto state = petEngine.snapshot();
  Serial.printf("[pet] restored=%s xp=%lu food=%lu level=%u state_bytes=%u engine_bytes=%u\n",
      size ? "yes" : "new", static_cast<unsigned long>(state.xp),
      static_cast<unsigned long>(state.totalFood), state.level,
      static_cast<unsigned>(sizeof(outgoing.bytes)), static_cast<unsigned>(sizeof(petEngine)));
  if (!size) submit(now);
  return true;
}

void service(uint32_t now) {
  petEngine.tick(now);
  if (petEngine.snapshot().rewardEvents != lastRewards ||
      static_cast<uint32_t>(now - lastCheckpoint) >= kCheckpointMs) submit(now);
}

bool flushPending(uint32_t timeoutMs) {
  if (!ready.load()) return false;
  const uint32_t target = requested.load();
  const uint32_t start = millis();
  while (static_cast<int32_t>(committed.load() - target) < 0) {
    if (millis() - start >= timeoutMs) return false;
    delay(1);
  }
  return true;
}

bool checkpoint(uint32_t timeoutMs) {
  const bool saved = submit(millis()) && flushPending(timeoutMs);
  if (!saved) Serial.println("[pet] checkpoint not committed; previous saved progress retained");
  return saved;
}
bool storageReady() { return ready.load(); }
bool savePending() { return requested.load() != committed.load(); }
uint32_t maxSaveMicros() { return maxWriteUs.load(); }
}
