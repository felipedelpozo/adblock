#include "PetEngine.h"

#include <string.h>

namespace pet {
namespace {

constexpr uint32_t kRewardCooldownMs = 60000U;
constexpr uint64_t kRewardWindowMs = 3600000ULL;
constexpr uint64_t kTodayPeriodMs = 86400000ULL;
constexpr uint32_t kMaxMetric = 100U;
constexpr uint8_t kMetricFloor = 20;
constexpr uint32_t kMagic = 0x31544550UL;  // "PET1" in little endian.
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderSize = 8;
constexpr size_t kChecksumSize = 4;
constexpr size_t kPayloadSize = PetEngine::kEncodedSize - kHeaderSize - kChecksumSize;

uint32_t saturatingAdd32(uint32_t left, uint32_t right) {
  const uint32_t result = left + right;
  return result < left ? UINT32_MAX : result;
}

uint64_t saturatingAdd64(uint64_t left, uint64_t right) {
  const uint64_t result = left + right;
  return result < left ? UINT64_MAX : result;
}

uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xffffffffUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

class Writer {
 public:
  Writer(uint8_t* data, size_t capacity) : data_(data), capacity_(capacity), at_(0), ok_(true) {}

  void u8(uint8_t value) {
    if (at_ >= capacity_) { ok_ = false; return; }
    data_[at_++] = value;
  }
  void u16(uint16_t value) {
    u8(static_cast<uint8_t>(value));
    u8(static_cast<uint8_t>(value >> 8));
  }
  void u32(uint32_t value) {
    u8(static_cast<uint8_t>(value));
    u8(static_cast<uint8_t>(value >> 8));
    u8(static_cast<uint8_t>(value >> 16));
    u8(static_cast<uint8_t>(value >> 24));
  }
  void u64(uint64_t value) {
    for (uint8_t i = 0; i < 8; ++i) u8(static_cast<uint8_t>(value >> (i * 8U)));
  }
  size_t position() const { return at_; }
  bool ok() const { return ok_; }

 private:
  uint8_t* data_;
  size_t capacity_;
  size_t at_;
  bool ok_;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t capacity) : data_(data), capacity_(capacity), at_(0), ok_(true) {}

  uint8_t u8() {
    if (at_ >= capacity_) { ok_ = false; return 0; }
    return data_[at_++];
  }
  uint16_t u16() {
    const uint16_t low = u8();
    const uint16_t high = u8();
    return low | static_cast<uint16_t>(high << 8);
  }
  uint32_t u32() {
    const uint32_t b0 = u8();
    const uint32_t b1 = u8();
    const uint32_t b2 = u8();
    const uint32_t b3 = u8();
    return b0 | b1 << 8 | b2 << 16 | b3 << 24;
  }
  uint64_t u64() {
    uint64_t value = 0;
    for (uint8_t i = 0; i < 8; ++i) value |= static_cast<uint64_t>(u8()) << (i * 8U);
    return value;
  }
  bool ok() const { return ok_; }

 private:
  const uint8_t* data_;
  size_t capacity_;
  size_t at_;
  bool ok_;
};

}  // namespace

PetEngine::PetEngine()
    : xp_(0), totalFood_(0), activeMs_(0), bornAt_(0), lastFed_(0), hunger_(100),
      happiness_(100), energy_(100), rewardEvents_(0), uniqueRewardedDomains_(0),
      clientCount_(0), decayHungerRemainder_(0), decayHappinessRemainder_(0),
      decayEnergyRemainder_(0), todayPeriodIndex_(0), todayBlocked_(0), todayAllowed_(0),
      todayFood_(0), todayRewardEvents_(0), todayUniqueRewardedDomains_(0), todayClientCount_(0),
      rewardAt_{}, rewardCount_(0), rewardNext_(0), cooldownHash_{}, cooldownAt_{},
      cooldownCount_(0), cooldownNext_(0), domainBloom_{}, clientBloom_{}, rewardClientBloom_{},
      todayDomainBloom_{}, todayClientBloom_{}, lastNowMs_(0),
      revision_(0), clockValid_(false) {}

void PetEngine::markDirty() {
  ++revision_;
}

void PetEngine::saturatingAdd(uint32_t& value, uint32_t amount) {
  value = saturatingAdd32(value, amount);
}

void PetEngine::addActiveMs(uint32_t elapsedMs) {
  if (!elapsedMs) return;
  activeMs_ = saturatingAdd64(activeMs_, elapsedMs);
  applyDecay(elapsedMs);
  markDirty();
}

void PetEngine::applyDecay(uint32_t elapsedMs) {
  const uint32_t hungerTotal = saturatingAdd32(decayHungerRemainder_, elapsedMs);
  const uint32_t happinessTotal = saturatingAdd32(decayHappinessRemainder_, elapsedMs);
  const uint32_t energyTotal = saturatingAdd32(decayEnergyRemainder_, elapsedMs);
  const uint32_t hungerSteps = hungerTotal / 900000U;
  const uint32_t happinessSteps = happinessTotal / 1800000U;
  const uint32_t energySteps = energyTotal / 1800000U;
  decayHungerRemainder_ = hungerTotal % 900000U;
  decayHappinessRemainder_ = happinessTotal % 1800000U;
  decayEnergyRemainder_ = energyTotal % 1800000U;
  hunger_ = static_cast<uint8_t>(hungerSteps >= hunger_ - kMetricFloor ? kMetricFloor : hunger_ - hungerSteps);
  happiness_ = static_cast<uint8_t>(happinessSteps >= happiness_ - kMetricFloor ? kMetricFloor : happiness_ - happinessSteps);
  energy_ = static_cast<uint8_t>(energySteps >= energy_ - kMetricFloor ? kMetricFloor : energy_ - energySteps);
}

void PetEngine::tick(uint32_t nowMs) {
  if (!clockValid_) {
    lastNowMs_ = nowMs;
    clockValid_ = true;
    return;
  }
  const uint32_t elapsed = nowMs - lastNowMs_;
  lastNowMs_ = nowMs;
  addActiveMs(elapsed);
}

uint64_t PetEngine::hashBytes(const char* input, size_t length) const {
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < length; ++i) {
    hash ^= static_cast<uint8_t>(input[i]);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool PetEngine::normalizeDomain(const char* input, char* output, size_t capacity) const {
  if (!input || !output || capacity < 2) return false;
  size_t begin = 0;
  size_t end = 0;
  while (input[end] != '\0') {
    if (++end > 253) return false;
  }
  while (begin < end && (input[begin] == ' ' || input[begin] == '\t' ||
                         input[begin] == '\r' || input[begin] == '\n')) ++begin;
  while (end > begin && (input[end - 1] == ' ' || input[end - 1] == '\t' ||
                         input[end - 1] == '\r' || input[end - 1] == '\n')) --end;
  if (end > begin && input[end - 1] == '.') --end;
  if (end <= begin || end - begin > 253 || end - begin + 1 > capacity) return false;

  size_t out = 0;
  size_t labelLength = 0;
  bool hasDot = false;
  bool labelStart = true;
  for (size_t i = begin; i < end; ++i) {
    const unsigned char ch = static_cast<unsigned char>(input[i]);
    if (ch == '.') {
      if (labelLength == 0 || labelLength > 63 || labelStart || output[out - 1] == '-') return false;
      output[out++] = '.';
      labelLength = 0;
      labelStart = true;
      hasDot = true;
      continue;
    }
    if (ch >= 'A' && ch <= 'Z') output[out++] = static_cast<char>(ch - 'A' + 'a');
    else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') output[out++] = static_cast<char>(ch);
    else return false;
    if (labelStart && output[out - 1] == '-') return false;
    labelStart = false;
    if (++labelLength > 63) return false;
  }
  if (!hasDot || labelLength == 0 || output[out - 1] == '-') return false;
  output[out] = '\0';
  return true;
}

bool PetEngine::bloomContains(const uint64_t* bloom, uint64_t hash) const {
  for (uint8_t probe = 0; probe < 4; ++probe) {
    const uint64_t mixed = hash + static_cast<uint64_t>(probe) * 0x9e3779b97f4a7c15ULL;
    const uint8_t bit = static_cast<uint8_t>((mixed ^ (mixed >> 32)) & 255U);
    if ((bloom[bit >> 6] & (1ULL << (bit & 63U))) == 0) return false;
  }
  return true;
}

void PetEngine::bloomInsert(uint64_t* bloom, uint64_t hash) {
  for (uint8_t probe = 0; probe < 4; ++probe) {
    const uint64_t mixed = hash + static_cast<uint64_t>(probe) * 0x9e3779b97f4a7c15ULL;
    const uint8_t bit = static_cast<uint8_t>((mixed ^ (mixed >> 32)) & 255U);
    bloom[bit >> 6] |= 1ULL << (bit & 63U);
  }
}

bool PetEngine::observeRewardClientAndReturnNovel(uint32_t client) {
  const uint64_t hash = static_cast<uint64_t>(client) * 0x9e3779b97f4a7c15ULL ^ 0xd6e8feb86659fd93ULL;
  const bool novel = !bloomContains(rewardClientBloom_, hash);
  if (novel) bloomInsert(rewardClientBloom_, hash);
  return novel;
}

void PetEngine::observeClient(uint32_t client) {
  const uint64_t hash = static_cast<uint64_t>(client) * 0x9e3779b97f4a7c15ULL ^ 0xd6e8feb86659fd93ULL;
  if (bloomContains(clientBloom_, hash)) return;
  bloomInsert(clientBloom_, hash);
  saturatingAdd(clientCount_, 1);
  markDirty();
}

bool PetEngine::cooldownActive(uint64_t hash) const {
  for (uint8_t i = 0; i < cooldownCount_; ++i) {
    if (cooldownHash_[i] != hash) continue;
    if (activeMs_ < cooldownAt_[i]) return true;
    return activeMs_ - cooldownAt_[i] < kRewardCooldownMs;
  }
  return false;
}

void PetEngine::rememberCooldown(uint64_t hash) {
  for (uint8_t i = 0; i < cooldownCount_; ++i) {
    if (cooldownHash_[i] == hash) {
      cooldownAt_[i] = activeMs_;
      markDirty();
      return;
    }
  }
  uint8_t index;
  if (cooldownCount_ < kCooldownCapacity) {
    index = cooldownCount_++;
  } else {
    index = cooldownNext_;
    cooldownNext_ = static_cast<uint8_t>((cooldownNext_ + 1) % kCooldownCapacity);
  }
  cooldownHash_[index] = hash;
  cooldownAt_[index] = activeMs_;
  markDirty();
}

void PetEngine::pruneRewardEvents() {
  while (rewardCount_ > 0) {
    const uint64_t at = rewardAt_[rewardNext_];
    if (activeMs_ < at || activeMs_ - at < kRewardWindowMs) break;
    rewardNext_ = static_cast<uint8_t>((rewardNext_ + 1) % kRewardEventCapacity);
    --rewardCount_;
    markDirty();
  }
}

void PetEngine::ensureTodayPeriod() {
  const uint64_t period = activeMs_ / kTodayPeriodMs;
  if (todayPeriodIndex_ == period) return;
  todayPeriodIndex_ = period;
  todayBlocked_ = 0;
  todayAllowed_ = 0;
  todayFood_ = 0;
  todayRewardEvents_ = 0;
  todayUniqueRewardedDomains_ = 0;
  todayClientCount_ = 0;
  memset(todayDomainBloom_, 0, sizeof(todayDomainBloom_));
  memset(todayClientBloom_, 0, sizeof(todayClientBloom_));
  markDirty();
}

void PetEngine::observeTodayClient(uint32_t client) {
  const uint64_t hash = static_cast<uint64_t>(client) * 0x9e3779b97f4a7c15ULL ^ 0xd6e8feb86659fd93ULL;
  if (bloomContains(todayClientBloom_, hash)) return;
  bloomInsert(todayClientBloom_, hash);
  saturatingAdd(todayClientCount_, 1);
  markDirty();
}

uint8_t PetEngine::levelForXp() const {
  if (xp_ >= 2000U) return 5;
  if (xp_ >= 1000U) return 4;
  if (xp_ >= 500U) return 3;
  if (xp_ >= 200U) return 2;
  if (xp_ >= 50U) return 1;
  return 0;
}

PetEngine::Species PetEngine::speciesForXp() const {
  switch (levelForXp()) {
    case 1: return Species::Hatchling;
    case 2: return Species::Blocky;
    case 3: return Species::AdHunter;
    case 4: return Species::AdEater;
    case 5: return Species::Void;
    default: return Species::Egg;
  }
}

uint8_t PetEngine::onBlockedDomain(const char* domain, uint32_t client, uint32_t nowMs) {
  tick(nowMs);
  char normalized[254];
  if (!normalizeDomain(domain, normalized, sizeof(normalized))) return 0;
  const uint64_t domainHash = hashBytes(normalized, strlen(normalized));
  ensureTodayPeriod();
  observeClient(client);
  observeTodayClient(client);
  pruneRewardEvents();
  if (cooldownActive(domainHash) || rewardCount_ >= kRewardEventCapacity) return 0;

  // Novelty is consumed only by an accepted reward. A cooldown or full quota
  // must not permanently discard the next legitimate client bonus.
  const bool novelClient = observeRewardClientAndReturnNovel(client);
  const bool novelDomain = !bloomContains(domainBloom_, domainHash);
  const bool novelTodayDomain = !bloomContains(todayDomainBloom_, domainHash);
  bloomInsert(domainBloom_, domainHash);
  bloomInsert(todayDomainBloom_, domainHash);
  if (novelDomain) saturatingAdd(uniqueRewardedDomains_, 1);
  if (novelTodayDomain) saturatingAdd(todayUniqueRewardedDomains_, 1);
  rememberCooldown(domainHash);
  rewardAt_[static_cast<size_t>((rewardNext_ + rewardCount_) % kRewardEventCapacity)] = activeMs_;
  ++rewardCount_;
  saturatingAdd(rewardEvents_, 1);
  const uint8_t bonus = static_cast<uint8_t>((novelDomain ? 1 : 0) + (novelClient ? 1 : 0));
  const uint8_t food = static_cast<uint8_t>(1 + bonus > 3 ? 3 : 1 + bonus);
  saturatingAdd(totalFood_, food);
  saturatingAdd(todayFood_, food);
  xp_ = saturatingAdd32(xp_, static_cast<uint32_t>(10 + bonus));
  saturatingAdd(todayRewardEvents_, 1);
  const uint32_t hungerBoost = static_cast<uint32_t>(food) * 4U;
  const uint32_t happinessBoost = static_cast<uint32_t>(food) * 3U;
  const uint32_t energyBoost = static_cast<uint32_t>(food) * 2U;
  hunger_ = static_cast<uint8_t>(hungerBoost + hunger_ > kMaxMetric ? kMaxMetric : hunger_ + hungerBoost);
  happiness_ = static_cast<uint8_t>(happinessBoost + happiness_ > kMaxMetric ? kMaxMetric : happiness_ + happinessBoost);
  energy_ = static_cast<uint8_t>(energyBoost + energy_ > kMaxMetric ? kMaxMetric : energy_ + energyBoost);
  lastFed_ = activeMs_;
  markDirty();
  return food;
}

void PetEngine::recordQuery(bool blocked, uint32_t client, uint32_t nowMs) {
  tick(nowMs);
  ensureTodayPeriod();
  observeClient(client);
  observeTodayClient(client);
  uint32_t& count = blocked ? todayBlocked_ : todayAllowed_;
  saturatingAdd(count, 1);
  markDirty();
}

PetEngine::Snapshot PetEngine::snapshot() const {
  Snapshot result = {};
  result.xp = xp_;
  result.totalFood = totalFood_;
  result.level = levelForXp();
  result.hunger = hunger_;
  result.happiness = happiness_;
  result.energy = energy_;
  result.species = speciesForXp();
  result.bornAt = bornAt_;
  result.lastFed = lastFed_;
  result.activeMs = activeMs_;
  result.rewardEvents = rewardEvents_;
  result.uniqueRewardedDomains = uniqueRewardedDomains_;
  result.clientCount = clientCount_;
  if (todayPeriodIndex_ == activeMs_ / kTodayPeriodMs) {
    result.blockedToday = todayBlocked_;
    result.allowedToday = todayAllowed_;
    result.foodToday = todayFood_;
    result.rewardEventsToday = todayRewardEvents_;
    result.uniqueRewardedDomainsToday = todayUniqueRewardedDomains_;
    result.clientCountToday = todayClientCount_;
  }
  return result;
}

bool PetEngine::encode(uint8_t* output, size_t capacity) const {
  if (!output || capacity < kEncodedSize) return false;
  memset(output, 0, kEncodedSize);
  Writer writer(output, kEncodedSize - kChecksumSize);
  writer.u32(kMagic);
  writer.u8(kVersion);
  writer.u8(0);
  writer.u16(static_cast<uint16_t>(kPayloadSize));
  writer.u32(xp_);
  writer.u32(totalFood_);
  writer.u64(activeMs_);
  writer.u64(bornAt_);
  writer.u64(lastFed_);
  writer.u8(hunger_);
  writer.u8(happiness_);
  writer.u8(energy_);
  writer.u8(cooldownCount_);
  writer.u8(cooldownNext_);
  writer.u8(rewardCount_);
  writer.u8(rewardNext_);
  writer.u8(0);
  writer.u32(rewardEvents_);
  writer.u32(uniqueRewardedDomains_);
  writer.u32(clientCount_);
  writer.u32(decayHungerRemainder_);
  writer.u32(decayHappinessRemainder_);
  writer.u32(decayEnergyRemainder_);
  writer.u64(todayPeriodIndex_);
  writer.u32(todayBlocked_);
  writer.u32(todayAllowed_);
  writer.u32(todayFood_);
  writer.u32(todayRewardEvents_);
  writer.u32(todayUniqueRewardedDomains_);
  writer.u32(todayClientCount_);
  for (size_t i = 0; i < kRewardEventCapacity; ++i) writer.u64(rewardAt_[i]);
  for (size_t i = 0; i < kCooldownCapacity; ++i) writer.u64(cooldownHash_[i]);
  for (size_t i = 0; i < kCooldownCapacity; ++i) writer.u64(cooldownAt_[i]);
  for (size_t i = 0; i < kBloomWords; ++i) writer.u64(domainBloom_[i]);
  for (size_t i = 0; i < kBloomWords; ++i) writer.u64(clientBloom_[i]);
  for (size_t i = 0; i < kBloomWords; ++i) writer.u64(rewardClientBloom_[i]);
  for (size_t i = 0; i < kBloomWords; ++i) writer.u64(todayDomainBloom_[i]);
  for (size_t i = 0; i < kBloomWords; ++i) writer.u64(todayClientBloom_[i]);
  if (!writer.ok() || writer.position() != kEncodedSize - kChecksumSize) return false;
  const uint32_t checksum = crc32(output, kEncodedSize - kChecksumSize);
  output[kEncodedSize - 4] = static_cast<uint8_t>(checksum);
  output[kEncodedSize - 3] = static_cast<uint8_t>(checksum >> 8);
  output[kEncodedSize - 2] = static_cast<uint8_t>(checksum >> 16);
  output[kEncodedSize - 1] = static_cast<uint8_t>(checksum >> 24);
  return true;
}

bool PetEngine::decode(const uint8_t* input, size_t length, uint32_t nowMs) {
  if (!input || length != kEncodedSize) return false;
  const uint32_t expected = static_cast<uint32_t>(input[kEncodedSize - 4]) |
      static_cast<uint32_t>(input[kEncodedSize - 3]) << 8 |
      static_cast<uint32_t>(input[kEncodedSize - 2]) << 16 |
      static_cast<uint32_t>(input[kEncodedSize - 1]) << 24;
  if (crc32(input, kEncodedSize - kChecksumSize) != expected) return false;
  Reader reader(input, kEncodedSize - kChecksumSize);
  if (reader.u32() != kMagic || reader.u8() != kVersion || reader.u8() != 0 || reader.u16() != kPayloadSize) return false;

  PetEngine candidate;
  candidate.xp_ = reader.u32();
  candidate.totalFood_ = reader.u32();
  candidate.activeMs_ = reader.u64();
  candidate.bornAt_ = reader.u64();
  candidate.lastFed_ = reader.u64();
  candidate.hunger_ = reader.u8();
  candidate.happiness_ = reader.u8();
  candidate.energy_ = reader.u8();
  candidate.cooldownCount_ = reader.u8();
  candidate.cooldownNext_ = reader.u8();
  candidate.rewardCount_ = reader.u8();
  candidate.rewardNext_ = reader.u8();
  (void)reader.u8();
  candidate.rewardEvents_ = reader.u32();
  candidate.uniqueRewardedDomains_ = reader.u32();
  candidate.clientCount_ = reader.u32();
  candidate.decayHungerRemainder_ = reader.u32();
  candidate.decayHappinessRemainder_ = reader.u32();
  candidate.decayEnergyRemainder_ = reader.u32();
  candidate.todayPeriodIndex_ = reader.u64();
  candidate.todayBlocked_ = reader.u32();
  candidate.todayAllowed_ = reader.u32();
  candidate.todayFood_ = reader.u32();
  candidate.todayRewardEvents_ = reader.u32();
  candidate.todayUniqueRewardedDomains_ = reader.u32();
  candidate.todayClientCount_ = reader.u32();
  for (size_t i = 0; i < kRewardEventCapacity; ++i) candidate.rewardAt_[i] = reader.u64();
  for (size_t i = 0; i < kCooldownCapacity; ++i) candidate.cooldownHash_[i] = reader.u64();
  for (size_t i = 0; i < kCooldownCapacity; ++i) candidate.cooldownAt_[i] = reader.u64();
  for (size_t i = 0; i < kBloomWords; ++i) candidate.domainBloom_[i] = reader.u64();
  for (size_t i = 0; i < kBloomWords; ++i) candidate.clientBloom_[i] = reader.u64();
  for (size_t i = 0; i < kBloomWords; ++i) candidate.rewardClientBloom_[i] = reader.u64();
  for (size_t i = 0; i < kBloomWords; ++i) candidate.todayDomainBloom_[i] = reader.u64();
  for (size_t i = 0; i < kBloomWords; ++i) candidate.todayClientBloom_[i] = reader.u64();
  if (!reader.ok() || candidate.hunger_ < kMetricFloor || candidate.hunger_ > 100 ||
      candidate.happiness_ < kMetricFloor || candidate.happiness_ > 100 ||
      candidate.energy_ < kMetricFloor || candidate.energy_ > 100 ||
      candidate.rewardCount_ > kRewardEventCapacity || candidate.rewardNext_ >= kRewardEventCapacity ||
      candidate.cooldownCount_ > kCooldownCapacity || candidate.cooldownNext_ >= kCooldownCapacity ||
      candidate.decayHungerRemainder_ >= 900000U || candidate.decayHappinessRemainder_ >= 1800000U ||
      candidate.decayEnergyRemainder_ >= 1800000U || candidate.lastFed_ > candidate.activeMs_ ||
      candidate.bornAt_ > candidate.activeMs_ || candidate.todayPeriodIndex_ > candidate.activeMs_ / kTodayPeriodMs) return false;
  for (size_t i = 0; i < kRewardEventCapacity; ++i) {
    if (candidate.rewardAt_[i] > candidate.activeMs_) return false;
  }
  for (size_t i = 0; i < kCooldownCapacity; ++i) {
    if (candidate.cooldownAt_[i] > candidate.activeMs_) return false;
  }
  candidate.lastNowMs_ = nowMs;
  candidate.clockValid_ = true;
  candidate.revision_ = revision_ + 1;
  *this = candidate;
  return true;
}

const char* speciesName(Species species) {
  switch (species) {
    case Species::Egg: return "Egg";
    case Species::Hatchling: return "Hatchling";
    case Species::Blocky: return "Blocky";
    case Species::AdHunter: return "AdHunter";
    case Species::AdEater: return "AdEater";
    case Species::Void: return "Void";
    default: return "Egg";
  }
}

}  // namespace pet
