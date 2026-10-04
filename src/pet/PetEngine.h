#pragma once

#include <stddef.h>
#include <stdint.h>

namespace pet {

class PetEngine {
 public:
  // Species is derived from XP. The names are intentionally stable because
  // they are consumed by the display layer and persisted state never stores
  // this derived value.
  enum class Species : uint8_t {
    Egg = 0,
    Hatchling = 1,
    Blocky = 2,
    AdHunter = 3,
    AdEater = 4,
    Void = 5,
  };

  struct Snapshot {
    uint32_t xp;
    uint32_t totalFood;
    uint8_t level;
    // 0..100 metrics; active-time decay stops at 20, so the pet never dies.
    uint8_t hunger;
    uint8_t happiness;
    uint8_t energy;
    Species species;
    // All three clocks are active uptime milliseconds, never wall clock.
    uint64_t bornAt;
    uint64_t lastFed;
    uint64_t activeMs;
    uint32_t blockedToday;
    uint32_t allowedToday;
    uint32_t foodToday;
    // The following three are lifetime counters backed by persistent bounded
    // filters; the Today fields are the current anchored 24h active period.
    uint32_t rewardEvents;
    uint32_t uniqueRewardedDomains;
    uint32_t clientCount;
    uint32_t rewardEventsToday;
    uint32_t uniqueRewardedDomainsToday;
    uint32_t clientCountToday;
  };

  // The fixed record is deliberately self-contained: no heap, Arduino, or
  // platform types are required. It includes a version, payload length, and
  // CRC32, so callers can reject torn or incompatible NVS values.
  static constexpr size_t kEncodedSize = 1532;

  PetEngine();

  void tick(uint32_t nowMs);

  // Returns food awarded for this blocked domain: 0 if malformed, rate limited,
  // or over the rolling active-hour quota; otherwise 1..3. Domain text is
  // normalized only for hashing and is never exposed in Snapshot.
  uint8_t onBlockedDomain(const char* domain, uint32_t client,
                          uint32_t nowMs);

  // Records every DNS query, whether blocked or allowed. Counts are reported
  // over the anchored 24-hour active-uptime period named "Today" by the API;
  // there is intentionally no wall-clock date or calendar-day dependency.
  void recordQuery(bool blocked, uint32_t client, uint32_t nowMs);

  Snapshot snapshot() const;
  uint32_t revision() const { return revision_; }

  bool encode(uint8_t* output, size_t capacity) const;
  bool decode(const uint8_t* input, size_t length, uint32_t nowMs);

 private:
  static constexpr size_t kRewardEventCapacity = 30;
  static constexpr size_t kCooldownCapacity = 64;
  static constexpr size_t kBloomWords = 4;

  uint32_t xp_;
  uint32_t totalFood_;
  uint64_t activeMs_;
  uint64_t bornAt_;
  uint64_t lastFed_;
  uint8_t hunger_;
  uint8_t happiness_;
  uint8_t energy_;

  uint32_t rewardEvents_;
  uint32_t uniqueRewardedDomains_;
  uint32_t clientCount_;

  uint32_t decayHungerRemainder_;
  uint32_t decayHappinessRemainder_;
  uint32_t decayEnergyRemainder_;

  uint64_t todayPeriodIndex_;
  uint32_t todayBlocked_;
  uint32_t todayAllowed_;
  uint32_t todayFood_;
  uint32_t todayRewardEvents_;
  uint32_t todayUniqueRewardedDomains_;
  uint32_t todayClientCount_;

  uint64_t rewardAt_[kRewardEventCapacity];
  uint8_t rewardCount_;
  uint8_t rewardNext_;

  uint64_t cooldownHash_[kCooldownCapacity];
  uint64_t cooldownAt_[kCooldownCapacity];
  uint8_t cooldownCount_;
  uint8_t cooldownNext_;

  // Bloom filters are persistent and bounded. A false positive suppresses a
  // novelty bonus (and may under-count unique values), which is conservative
  // for anti-farming and avoids storing domains or client identifiers.
  uint64_t domainBloom_[kBloomWords];
  uint64_t clientBloom_[kBloomWords];
  uint64_t rewardClientBloom_[kBloomWords];
  uint64_t todayDomainBloom_[kBloomWords];
  uint64_t todayClientBloom_[kBloomWords];

  uint32_t lastNowMs_;
  uint32_t revision_;
  bool clockValid_;

  void markDirty();
  void addActiveMs(uint32_t elapsedMs);
  void applyDecay(uint32_t elapsedMs);
  void observeClient(uint32_t client);
  bool observeRewardClientAndReturnNovel(uint32_t client);
  bool bloomContains(const uint64_t* bloom, uint64_t hash) const;
  void bloomInsert(uint64_t* bloom, uint64_t hash);
  bool normalizeDomain(const char* input, char* output, size_t capacity) const;
  uint64_t hashBytes(const char* input, size_t length) const;
  bool cooldownActive(uint64_t hash) const;
  void rememberCooldown(uint64_t hash);
  void pruneRewardEvents();
  void ensureTodayPeriod();
  void observeTodayClient(uint32_t client);
  uint8_t levelForXp() const;
  Species speciesForXp() const;
  void saturatingAdd(uint32_t& value, uint32_t amount);
};

using Species = PetEngine::Species;
using Snapshot = PetEngine::Snapshot;

const char* speciesName(Species species);

}  // namespace pet
