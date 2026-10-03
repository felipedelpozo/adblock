#include <unity.h>
#include "updater_model.h"
#include "github_manifest.h"

void test_semver() {
  TEST_ASSERT_TRUE(updater_model::strictSemver("1.2.3"));
  TEST_ASSERT_TRUE(updater_model::strictSemver("v10.0.1"));
  TEST_ASSERT_FALSE(updater_model::strictSemver("1.2"));
  TEST_ASSERT_FALSE(updater_model::strictSemver("1.02.3"));
  TEST_ASSERT_FALSE(updater_model::strictSemver("1.2.3-beta"));
  TEST_ASSERT_FALSE(updater_model::strictSemver("999999999999.2.3"));
  TEST_ASSERT_EQUAL_INT(-1, updater_model::compareSemver("1.2.3", "1.3.0"));
}

void test_digest_rejection_and_split_identity() {
  uint8_t digest[32] = {};
  TEST_ASSERT_TRUE(updater_model::digestMatches(std::string(64, '0'), digest));
  digest[17] = 1;
  TEST_ASSERT_FALSE(updater_model::digestMatches(std::string(64, '0'), digest));
  TEST_ASSERT_FALSE(updater_model::digestMatches(std::string(64, 'x'), digest));
  const char marker[] = "xxxxADBLOCK_ID:jc3636w518c:1.2.3";
  updater_model::IdentityMatcher match("jc3636w518c", "1.2.3");
  match.write(reinterpret_cast<const uint8_t*>(marker), 13);
  TEST_ASSERT_FALSE(match.found());
  match.write(reinterpret_cast<const uint8_t*>(marker) + 13, sizeof(marker) - 13);
  TEST_ASSERT_TRUE(match.found());
  updater_model::IdentityMatcher wrong("s3-headless", "1.2.3");
  wrong.write(reinterpret_cast<const uint8_t*>(marker), sizeof(marker));
  TEST_ASSERT_FALSE(wrong.found());
  updater_model::IdentityMatcher wrongVersion("jc3636w518c", "1.2.30");
  wrongVersion.write(reinterpret_cast<const uint8_t*>(marker), sizeof(marker));
  TEST_ASSERT_FALSE(wrongVersion.found());
}

std::string validManifest() {
  return "{\n\"schema\": 1, \"repository\": \"felipedelpozo/adblock\",\"version\":\"1.2.3\","
         "\"builds\":[{\"profile\":\"jc3636w518c\",\"version\":\"1.2.3\",\"chip\":\"ESP32-S3\","
         "\"board\":\"jc3636w518c\",\"asset\":\"firmware-jc3636w518c.bin\",\"size\": 1234567,"
         "\"url\":\"https://github.com/felipedelpozo/adblock/releases/download/v1.2.3/firmware-jc3636w518c.bin\","
         "\"sha256\":\"" + std::string(64, 'a') + "\"}]}";
}

bool parseManifest(const std::string& text) {
  github_manifest::Build build;
  return github_manifest::parseBuild(text, "v1.2.3", "jc3636w518c", "ESP32-S3", "jc3636w518c", build);
}

void test_manifest_numeric_size_and_strict_contract() {
  github_manifest::Build build;
  TEST_ASSERT_TRUE(github_manifest::parseBuild(validManifest(), "v1.2.3", "jc3636w518c", "ESP32-S3", "jc3636w518c", build));
  TEST_ASSERT_EQUAL_UINT32(1234567, build.size);
  struct Mutation { const char* from; const char* to; };
  const Mutation invalid[] = {{"1234567", "\"1234567\""}, {"1234567", "-1"},
    {"1234567", "4294967296"}, {"1234567", "0"}, {"ESP32-S3", "ESP32-C3"},
    {"\"schema\": 1", "\"schema\": \"1\""}, {"felipedelpozo/adblock", "other/adblock"},
    {"v1.2.3/firmware", "v1.2.4/firmware"}, {"https://github.com", "https://evil.example/github.com"}};
  for (const Mutation& mutation : invalid) {
    std::string value = validManifest();
    const auto at = value.find(mutation.from);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, at);
    value.replace(at, std::strlen(mutation.from), mutation.to);
    TEST_ASSERT_FALSE(parseManifest(value));
  }
  TEST_ASSERT_FALSE(parseManifest("{}"));
  TEST_ASSERT_FALSE(parseManifest("not json"));
}

void test_release_whitespace_and_canonical_manifest() {
  std::string tag, url;
  const std::string release = "{\"draft\": false, \"prerelease\": false, \"tag_name\": \"v1.2.3\","
    "\"assets\": [{\"name\": \"manifest.json\", \"browser_download_url\": "
    "\"https://github.com/felipedelpozo/adblock/releases/download/v1.2.3/manifest.json\"}]}";
  TEST_ASSERT_TRUE(github_manifest::parseRelease(release, tag, url));
  std::string invalid = release;
  invalid.replace(invalid.find("https://github.com"), 18, "https://evil.test");
  TEST_ASSERT_FALSE(github_manifest::parseRelease(invalid, tag, url));
  invalid = release;
  invalid.replace(invalid.find("false"), 5, "true");
  TEST_ASSERT_FALSE(github_manifest::parseRelease(invalid, tag, url));
  invalid = release;
  invalid.replace(invalid.find("v1.2.3"), 6, "v1.2.3-beta");
  TEST_ASSERT_FALSE(github_manifest::parseRelease(invalid, tag, url));
}

void test_digest_and_url_contract() {
  TEST_ASSERT_TRUE(updater_model::strictSha256(std::string(64, 'a')));
  TEST_ASSERT_FALSE(updater_model::strictSha256(std::string(63, 'a')));
  TEST_ASSERT_FALSE(updater_model::strictSha256(std::string(64, 'A')));
  TEST_ASSERT_TRUE(updater_model::canonicalAssetUrl("https://github.com/felipedelpozo/adblock/releases/download/v1.2.3/firmware-c3.bin", "v1.2.3", "firmware-c3.bin"));
  TEST_ASSERT_FALSE(updater_model::canonicalAssetUrl("https://evil.example/github.com/felipedelpozo/adblock/releases/download/v1.2.3/firmware-c3.bin", "v1.2.3", "firmware-c3.bin"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_semver);
  RUN_TEST(test_digest_and_url_contract);
  RUN_TEST(test_digest_rejection_and_split_identity);
  RUN_TEST(test_manifest_numeric_size_and_strict_contract);
  RUN_TEST(test_release_whitespace_and_canonical_manifest);
  return UNITY_END();
}
