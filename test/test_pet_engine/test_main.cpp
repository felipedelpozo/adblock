#include <unity.h>

#include <stdio.h>
#include <string.h>

#include "pet/PetEngine.h"
#include "../../src/pet/PetEngine.cpp"

using pet::PetEngine;

uint32_t testCrc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xffffffffUL;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320UL & (0U - (crc & 1U)));
    }
  }
  return ~crc;
}

void refreshTestCrc(uint8_t* encoded) {
  const uint32_t crc = testCrc32(encoded, PetEngine::kEncodedSize - 4);
  encoded[PetEngine::kEncodedSize - 4] = static_cast<uint8_t>(crc);
  encoded[PetEngine::kEncodedSize - 3] = static_cast<uint8_t>(crc >> 8);
  encoded[PetEngine::kEncodedSize - 2] = static_cast<uint8_t>(crc >> 16);
  encoded[PetEngine::kEncodedSize - 1] = static_cast<uint8_t>(crc >> 24);
}

void test_reward_normalization_bonuses_and_cooldown() {
  PetEngine engine;
  TEST_ASSERT_EQUAL_UINT8(3, engine.onBlockedDomain(" Ads.Example.COM. ", 1, 100));
  PetEngine::Snapshot first = engine.snapshot();
  TEST_ASSERT_EQUAL_UINT32(12, first.xp);
  TEST_ASSERT_EQUAL_UINT32(3, first.totalFood);
  TEST_ASSERT_EQUAL_UINT32(1, first.rewardEvents);
  TEST_ASSERT_EQUAL_UINT32(1, first.uniqueRewardedDomains);
  TEST_ASSERT_EQUAL_UINT32(1, first.clientCount);
  TEST_ASSERT_EQUAL_UINT8(0, engine.onBlockedDomain("ads.example.com", 1, 100 + 59999));
  TEST_ASSERT_EQUAL_UINT8(1, engine.onBlockedDomain("ADS.EXAMPLE.COM", 1, 100 + 60000));
  TEST_ASSERT_EQUAL_UINT32(22, engine.snapshot().xp);
}

void test_hourly_quota_and_active_time_window() {
  PetEngine engine;
  for (uint32_t i = 0; i < 30; ++i) {
    char domain[48];
    snprintf(domain, sizeof(domain), "ad%u.example.com", static_cast<unsigned>(i));
    TEST_ASSERT_TRUE(engine.onBlockedDomain(domain, i + 1, 0) > 0);
  }
  TEST_ASSERT_EQUAL_UINT8(0, engine.onBlockedDomain("ad30.example.com", 31, 1));
  TEST_ASSERT_EQUAL_UINT8(0, engine.onBlockedDomain("ad30.example.com", 31, 3600000 - 1));
  TEST_ASSERT_EQUAL_UINT8(3, engine.onBlockedDomain("ad30.example.com", 31, 3600000));
}

void test_query_period_and_millis_wrap() {
  PetEngine engine;
  engine.recordQuery(true, 0x01020304U, UINT32_MAX - 10U);
  engine.recordQuery(false, 0x01020304U, 5U);
  PetEngine::Snapshot snapshot = engine.snapshot();
  TEST_ASSERT_EQUAL_UINT64(16, snapshot.activeMs);
  TEST_ASSERT_EQUAL_UINT32(1, snapshot.blockedToday);
  TEST_ASSERT_EQUAL_UINT32(1, snapshot.allowedToday);
  TEST_ASSERT_EQUAL_UINT32(1, snapshot.clientCount);
}

void test_decay_is_active_only_and_persistence_is_checked() {
  PetEngine engine;
  engine.tick(1000);
  engine.tick(901000);
  TEST_ASSERT_EQUAL_UINT8(99, engine.snapshot().hunger);
  TEST_ASSERT_EQUAL_UINT8(100, engine.snapshot().happiness);
  uint8_t encoded[PetEngine::kEncodedSize];
  TEST_ASSERT_TRUE(engine.encode(encoded, sizeof(encoded)));
  PetEngine restored;
  TEST_ASSERT_TRUE(restored.decode(encoded, sizeof(encoded), 900000));
  TEST_ASSERT_EQUAL_UINT64(engine.snapshot().activeMs, restored.snapshot().activeMs);
  restored.tick(900001);
  TEST_ASSERT_EQUAL_UINT64(engine.snapshot().activeMs + 1, restored.snapshot().activeMs);
  encoded[PetEngine::kEncodedSize - 1] ^= 0x80;
  TEST_ASSERT_FALSE(restored.decode(encoded, sizeof(encoded), 900002));
}

void test_evolution_names_and_invalid_domains() {
  PetEngine engine;
  TEST_ASSERT_EQUAL_STRING("Egg", pet::speciesName(PetEngine::Species::Egg));
  TEST_ASSERT_EQUAL_UINT8(0, engine.onBlockedDomain("localhost", 1, 0));
  TEST_ASSERT_EQUAL_UINT8(0, engine.onBlockedDomain("bad..example.com", 1, 1));
  TEST_ASSERT_EQUAL_UINT32(0, engine.snapshot().rewardEvents);
}

void test_query_observation_does_not_consume_reward_client_novelty() {
  PetEngine engine;
  engine.recordQuery(false, 77, 0);
  TEST_ASSERT_EQUAL_UINT8(3, engine.onBlockedDomain("fresh.example.com", 77, 1));
  TEST_ASSERT_EQUAL_UINT32(1, engine.snapshot().clientCount);
}

void test_all_evolution_stages_and_names() {
  PetEngine engine;
  TEST_ASSERT_EQUAL_INT(PetEngine::Species::Egg, engine.snapshot().species);
  uint32_t now = 0;
  uint32_t event = 0;
  while (engine.snapshot().xp < 2000) {
    char domain[64];
    snprintf(domain, sizeof(domain), "stage%u.example.com", static_cast<unsigned>(event));
    now += 3600001U;
    engine.onBlockedDomain(domain, event + 1, now);
    ++event;
    const uint32_t xp = engine.snapshot().xp;
    if (xp >= 50 && xp < 200) TEST_ASSERT_EQUAL_INT(PetEngine::Species::Hatchling, engine.snapshot().species);
    if (xp >= 200 && xp < 500) TEST_ASSERT_EQUAL_INT(PetEngine::Species::Blocky, engine.snapshot().species);
    if (xp >= 500 && xp < 1000) TEST_ASSERT_EQUAL_INT(PetEngine::Species::AdHunter, engine.snapshot().species);
    if (xp >= 1000 && xp < 2000) TEST_ASSERT_EQUAL_INT(PetEngine::Species::AdEater, engine.snapshot().species);
  }
  TEST_ASSERT_EQUAL_INT(PetEngine::Species::Void, engine.snapshot().species);
  TEST_ASSERT_EQUAL_STRING("Hatchling", pet::speciesName(PetEngine::Species::Hatchling));
  TEST_ASSERT_EQUAL_STRING("Blocky", pet::speciesName(PetEngine::Species::Blocky));
  TEST_ASSERT_EQUAL_STRING("AdHunter", pet::speciesName(PetEngine::Species::AdHunter));
  TEST_ASSERT_EQUAL_STRING("AdEater", pet::speciesName(PetEngine::Species::AdEater));
  TEST_ASSERT_EQUAL_STRING("Void", pet::speciesName(PetEngine::Species::Void));
}

void test_quota_cooldown_and_today_counters_survive_reboot() {
  PetEngine engine;
  engine.recordQuery(true, 99, 0);
  TEST_ASSERT_EQUAL_UINT8(3, engine.onBlockedDomain("persist.example.com", 99, 1));
  for (uint32_t i = 0; i < 29; ++i) {
    char domain[64];
    snprintf(domain, sizeof(domain), "quota%u.example.com", static_cast<unsigned>(i));
    TEST_ASSERT_TRUE(engine.onBlockedDomain(domain, i + 100, 2) > 0);
  }
  uint8_t encoded[PetEngine::kEncodedSize];
  TEST_ASSERT_TRUE(engine.encode(encoded, sizeof(encoded)));
  PetEngine restored;
  TEST_ASSERT_TRUE(restored.decode(encoded, sizeof(encoded), 500000));
  TEST_ASSERT_EQUAL_UINT32(30, restored.snapshot().rewardEventsToday);
  TEST_ASSERT_EQUAL_UINT32(30, restored.snapshot().clientCountToday);
  TEST_ASSERT_EQUAL_UINT8(0, restored.onBlockedDomain("after-quota.example.com", 500, 500001));
  TEST_ASSERT_TRUE(restored.onBlockedDomain("after-quota.example.com", 500, 4100001) > 0);

  PetEngine cooldownSource;
  TEST_ASSERT_TRUE(cooldownSource.onBlockedDomain("reboot.example.com", 7, 0) > 0);
  TEST_ASSERT_TRUE(cooldownSource.encode(encoded, sizeof(encoded)));
  PetEngine cooldownRestored;
  TEST_ASSERT_TRUE(cooldownRestored.decode(encoded, sizeof(encoded), 100000));
  TEST_ASSERT_EQUAL_UINT8(0, cooldownRestored.onBlockedDomain("reboot.example.com", 7, 100001));
  TEST_ASSERT_TRUE(cooldownRestored.onBlockedDomain("reboot.example.com", 7, 160001) > 0);
}

void test_cooldown_cache_eviction_serializes_after_many_domains() {
  PetEngine engine;
  uint32_t now = 0;
  for (uint32_t i = 0; i < 130; ++i) {
    char domain[64];
    snprintf(domain, sizeof(domain), "cache%u.example.com", static_cast<unsigned>(i));
    now += 3600001U;
    TEST_ASSERT_TRUE(engine.onBlockedDomain(domain, i + 1, now) > 0);
  }
  uint8_t encoded[PetEngine::kEncodedSize];
  TEST_ASSERT_TRUE(engine.encode(encoded, sizeof(encoded)));
  PetEngine restored;
  TEST_ASSERT_TRUE(restored.decode(encoded, sizeof(encoded), 123));
}

void test_decode_rejects_empty_truncated_version_corruption_and_timestamp() {
  PetEngine engine;
  uint8_t encoded[PetEngine::kEncodedSize];
  TEST_ASSERT_TRUE(engine.encode(encoded, sizeof(encoded)));
  PetEngine restored;
  TEST_ASSERT_FALSE(restored.decode(nullptr, 0, 0));
  TEST_ASSERT_FALSE(restored.decode(encoded, sizeof(encoded) - 1, 0));

  encoded[4] = 2;
  refreshTestCrc(encoded);
  TEST_ASSERT_FALSE(restored.decode(encoded, sizeof(encoded), 0));
  TEST_ASSERT_TRUE(engine.encode(encoded, sizeof(encoded)));
  encoded[0] ^= 0x01;
  TEST_ASSERT_FALSE(restored.decode(encoded, sizeof(encoded), 0));
  TEST_ASSERT_TRUE(engine.encode(encoded, sizeof(encoded)));
  // Payload offset 72 is todayPeriodIndex; period 1 cannot accompany activeMs 0.
  encoded[72] = 1;
  refreshTestCrc(encoded);
  TEST_ASSERT_FALSE(restored.decode(encoded, sizeof(encoded), 0));
}

void test_today_fields_reset_on_next_active_period_but_lifetime_remains() {
  PetEngine engine;
  engine.recordQuery(true, 1, 0);
  TEST_ASSERT_TRUE(engine.onBlockedDomain("day.example.com", 1, 1) > 0);
  TEST_ASSERT_EQUAL_UINT32(1, engine.snapshot().blockedToday);
  TEST_ASSERT_EQUAL_UINT32(1, engine.snapshot().foodToday > 0 ? 1 : 0);
  engine.recordQuery(false, 2, 86400001U);
  PetEngine::Snapshot next = engine.snapshot();
  TEST_ASSERT_EQUAL_UINT32(0, next.blockedToday);
  TEST_ASSERT_EQUAL_UINT32(1, next.allowedToday);
  TEST_ASSERT_EQUAL_UINT32(0, next.foodToday);
  TEST_ASSERT_EQUAL_UINT32(0, next.rewardEventsToday);
  TEST_ASSERT_EQUAL_UINT32(1, next.clientCountToday);
  TEST_ASSERT_EQUAL_UINT32(1, next.rewardEvents);
  TEST_ASSERT_TRUE(next.totalFood > 0);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reward_normalization_bonuses_and_cooldown);
  RUN_TEST(test_hourly_quota_and_active_time_window);
  RUN_TEST(test_query_period_and_millis_wrap);
  RUN_TEST(test_decay_is_active_only_and_persistence_is_checked);
  RUN_TEST(test_evolution_names_and_invalid_domains);
  RUN_TEST(test_query_observation_does_not_consume_reward_client_novelty);
  RUN_TEST(test_all_evolution_stages_and_names);
  RUN_TEST(test_quota_cooldown_and_today_counters_survive_reboot);
  RUN_TEST(test_cooldown_cache_eviction_serializes_after_many_domains);
  RUN_TEST(test_decode_rejects_empty_truncated_version_corruption_and_timestamp);
  RUN_TEST(test_today_fields_reset_on_next_active_period_but_lifetime_remains);
  return UNITY_END();
}
