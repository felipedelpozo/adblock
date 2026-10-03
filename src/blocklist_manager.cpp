#include "blocklist_manager.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include "github_roots.h"
#include "blocklist_model.h"

namespace {
const char* kLivePath = "/blocklist.bin";
const char* kStagePath = "/blocklist.new";
const char* kMarkerPath = "/blocklist.commit";
const char* kConfigPath = "/lists.cfg";
const char* kConfigTempPath = "/lists.cfg.new";
const char* kManifestUrl = "https://github.com/felipedelpozo/adblock/releases/download/blocklists/manifest.json";
const char* kAssetPrefix = "https://github.com/felipedelpozo/adblock/releases/download/blocklists/blocklist-";
const size_t kHashBytes = 5;
const uint32_t kMaxListBytes = 2UL * 1024UL * 1024UL;
const uint32_t kMaxListDomains = kMaxListBytes / kHashBytes;
const uint32_t kHeadroomBytes = 8192;
SemaphoreHandle_t filesystemMutex = nullptr;

bool expired(uint32_t deadline) { return static_cast<int32_t>(millis() - deadline) >= 0; }

class BoundedTextSink final : public Stream {
 public:
  explicit BoundedTextSink(size_t limit) : limit_(limit) { value_.reserve(limit); }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (length > limit_ - value_.length() || !value_.concat(reinterpret_cast<const char*>(data), length)) return 0;
    return length;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  const String& value() const { return value_; }
 private:
  String value_;
  size_t limit_;
};

class DownloadSink final : public Stream {
 public:
  DownloadSink(BlocklistManager& owner, size_t limit, uint32_t deadline, mbedtls_sha256_context* digest = nullptr)
      : owner_(owner), limit_(limit), deadline_(deadline), digest_(digest) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (expired(deadline_) || length > limit_ - total_ || !owner_.writeStaged(data, length)) return 0;
    if (digest_) mbedtls_sha256_update_ret(digest_, data, length);
    total_ += length;
    return length;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t total() const { return total_; }
 private:
  BlocklistManager& owner_;
  size_t limit_;
  uint32_t deadline_;
  mbedtls_sha256_context* digest_;
  size_t total_ = 0;
};

}  // namespace

bool blocklistFilesystemLock(uint32_t timeoutMs) {
  return filesystemMutex && xSemaphoreTake(filesystemMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

void blocklistFilesystemUnlock() {
  if (filesystemMutex) xSemaphoreGive(filesystemMutex);
}

BlocklistManager blocklistManager;

const char* BlocklistManager::profileName(Profile profile) {
  switch (profile) {
    case Profile::Light: return "light";
    case Profile::Balanced: return "balanced";
    case Profile::Strict: return "strict";
    default: return "custom";
  }
}

bool BlocklistManager::profileFromName(const char* value, Profile& profile) {
  if (!value) return false;
  if (!strcmp(value, "custom")) profile = Profile::Custom;
  else if (!strcmp(value, "light")) profile = Profile::Light;
  else if (!strcmp(value, "balanced")) profile = Profile::Balanced;
  else if (!strcmp(value, "strict")) profile = Profile::Strict;
  else return false;
  return true;
}

bool BlocklistManager::setStateMutex(uint32_t timeoutMs) const {
  return stateMutex_ && xSemaphoreTake(stateMutex_, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
void BlocklistManager::releaseStateMutex() const {
  if (stateMutex_) xSemaphoreGive(stateMutex_);
}

void BlocklistManager::setStatus(const String& value) {
  if (setStateMutex(100)) { status_ = value; releaseStateMutex(); }
}

void BlocklistManager::setFailure(const String& value) {
  if (setStateMutex(100)) { failureStatus_ = value; status_ = value; releaseStateMutex(); }
}

String BlocklistManager::status() const {
  if (!setStateMutex(20)) return "Estado ocupado";
  const String value = status_;
  releaseStateMutex();
  return value;
}

BlocklistManager::Profile BlocklistManager::selectedProfile() const { return selected_; }
BlocklistManager::Profile BlocklistManager::appliedProfile() const { return applied_; }
String BlocklistManager::selectedName() const { return String(profileName(selected_)); }
String BlocklistManager::appliedName() const { return String(profileName(applied_)); }

bool BlocklistManager::validSha256(const String& value) {
  if (value.length() != 64) return false;
  for (size_t i = 0; i < 64; ++i) {
    const char ch = value[i];
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  }
  return true;
}

bool BlocklistManager::canonicalAssetUrl(const String& url, Profile profile, const String& sha256) {
  return validSha256(sha256) && url == String(kAssetPrefix) + profileName(profile) + "-" + sha256 + ".bin";
}

bool BlocklistManager::persistConfig(Profile selected, Profile applied) {
  if (!blocklistFilesystemLock(1000)) return false;
  LittleFS.remove(kConfigTempPath);
  File f = LittleFS.open(kConfigTempPath, "w");
  if (!f) { blocklistFilesystemUnlock(); return false; }
  const String contents = String(profileName(selected)) + "\n" + profileName(applied) + "\n";
  const bool written = f.print(contents) == contents.length();
  f.flush();
  f.close();
  const bool renamed = written && LittleFS.rename(kConfigTempPath, kConfigPath);
  if (!renamed) LittleFS.remove(kConfigTempPath);
  blocklistFilesystemUnlock();
  return renamed;
}

void BlocklistManager::loadConfig() {
  selected_ = applied_ = Profile::Custom;
  if (!LittleFS.exists(kConfigPath)) return;
  File f = LittleFS.open(kConfigPath, "r");
  if (!f) return;
  String selected = f.readStringUntil('\n');
  String applied = f.readStringUntil('\n');
  selected.trim(); applied.trim();
  Profile parsedSelected, parsedApplied;
  if (profileFromName(selected.c_str(), parsedSelected)) selected_ = parsedSelected;
  if (profileFromName(applied.c_str(), parsedApplied)) applied_ = parsedApplied;
  f.close();
}

bool BlocklistManager::validateFile(const char* path, uint32_t& domains, uint32_t& bytes) const {
  domains = bytes = 0;
  if (!blocklistFilesystemLock(1000)) return false;
  File f = LittleFS.open(path, "r");
  if (!f) { blocklistFilesystemUnlock(); return false; }
  const size_t size = f.size();
  if (size == 0 || size > kMaxListBytes || size % kHashBytes != 0) {
    f.close(); blocklistFilesystemUnlock(); return false;
  }
  uint8_t previous[kHashBytes] = {};
  uint8_t current[kHashBytes] = {};
  bool hasPrevious = false;
  while (f.available()) {
    if (f.read(current, kHashBytes) != kHashBytes) { f.close(); blocklistFilesystemUnlock(); return false; }
    if (hasPrevious && !blocklist_model::lessLittleEndian40(previous, current)) {
      f.close(); blocklistFilesystemUnlock(); return false;
    }
    memcpy(previous, current, kHashBytes);
    hasPrevious = true;
    ++domains;
  }
  bytes = static_cast<uint32_t>(size);
  f.close();
  blocklistFilesystemUnlock();
  return domains > 0;
}

bool BlocklistManager::recoverStage() {
  String marker;
  if (LittleFS.exists(kMarkerPath)) {
    File f = LittleFS.open(kMarkerPath, "r");
    if (f) { marker = f.readStringUntil('\n'); marker.trim(); f.close(); }
  }
  // A crash after the live rename but before the config commit leaves the
  // marker with no staging file.  Recover the applied profile from it.
  if (!LittleFS.exists(kStagePath) && marker.length()) {
    Profile profile;
    uint32_t domains = 0, bytes = 0;
    if (profileFromName(marker.c_str(), profile) && validateFile(kLivePath, domains, bytes)) {
      selected_ = applied_ = profile;
      const bool saved = persistConfig(selected_, applied_);
      if (!saved) return false;
    }
    if (blocklistFilesystemLock(1000)) { LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock(); }
    return true;
  }
  if (!LittleFS.exists(kStagePath)) return true;
  uint32_t domains = 0, bytes = 0;
  if (marker.length() && validateFile(kStagePath, domains, bytes)) {
    Profile profile;
    if (profileFromName(marker.c_str(), profile)) {
      if (!blocklistFilesystemLock(1000)) return false;
      const bool renamed = LittleFS.rename(kStagePath, kLivePath);
      if (renamed) {
        selected_ = applied_ = profile;
        domains_ = domains; bytes_ = bytes;
      }
      blocklistFilesystemUnlock();
      if (!renamed) return false;
      const bool saved = persistConfig(selected_, applied_);
      if (saved && blocklistFilesystemLock(1000)) { LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock(); }
      return saved;
    }
  }
  if (blocklistFilesystemLock(1000)) {
    LittleFS.remove(kStagePath);
    LittleFS.remove(kMarkerPath);
    blocklistFilesystemUnlock();
  }
  return true;
}

bool BlocklistManager::begin() {
  if (!filesystemMutex) filesystemMutex = xSemaphoreCreateMutex();
  if (!stateMutex_) stateMutex_ = xSemaphoreCreateMutex();
  if (!filesystemMutex || !stateMutex_) { setStatus("Coordinador de listas no disponible"); return false; }
  const bool hadConfig = LittleFS.exists(kConfigPath);
  loadConfig();
  recoverStage();
  if (blocklistFilesystemLock(1000)) {
    live_ = LittleFS.open(kLivePath, "r");
    if (live_) {
      bytes_ = live_.size();
      domains_ = (bytes_ > 0 && bytes_ % kHashBytes == 0) ? bytes_ / kHashBytes : 0;
    }
    blocklistFilesystemUnlock();
  }
  if (!hadConfig && !persistConfig(selected_, applied_)) setStatus("No se pudo guardar el estado de listas");
  if (!task_) {
    xTaskCreatePinnedToCore(taskEntry, "blocklist", 8192, this, 1, &task_, 0);
  }
  return true;
}

bool BlocklistManager::contains(uint64_t hash) const {
  if (!domains_) return false;
  int32_t lo = 0, hi = static_cast<int32_t>(domains_) - 1;
  uint8_t bytes[kHashBytes];
  while (lo <= hi) {
    const int32_t mid = (lo + hi) >> 1;
    live_.seek(static_cast<uint32_t>(mid) * kHashBytes);
    if (live_.read(bytes, kHashBytes) != kHashBytes) break;
    uint64_t value = 0;
    for (size_t i = 0; i < kHashBytes; ++i) value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    if (value < hash) lo = mid + 1;
    else if (value > hash) hi = mid - 1;
    else return true;
  }
  return false;
}

bool BlocklistManager::hasSpace(uint32_t newBytes) const {
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  const size_t free = total > used ? total - used : 0;
  return newBytes > 0 && newBytes <= kMaxListBytes && free >= static_cast<size_t>(newBytes) + kHeadroomBytes;
}

bool BlocklistManager::startStage(Profile profile, uint32_t expectedSize, uint32_t expectedDomains,
                                  const String& expectedSha256) {
  if (expectedSize > kMaxListBytes || (expectedSize && expectedSize % kHashBytes != 0) ||
      expectedDomains > kMaxListDomains || (expectedDomains && expectedSize != expectedDomains * kHashBytes) ||
      (expectedSha256.length() && !validSha256(expectedSha256))) return false;
  if (expectedSize && !hasSpace(expectedSize)) { setFailure("Espacio insuficiente para conservar la lista activa"); return false; }
  if (!blocklistFilesystemLock(1000)) return false;
  LittleFS.remove(kStagePath);
  LittleFS.remove(kMarkerPath);
  stage_ = LittleFS.open(kStagePath, "w");
  blocklistFilesystemUnlock();
  if (!stage_) return false;
  stageProfile_ = profile;
  stageExpectedSize_ = expectedSize;
  stageExpectedDomains_ = expectedDomains;
  stageExpectedSha256_ = expectedSha256;
  recordLength_ = 0; hasPrevious_ = false; stageDomains_ = 0; stageBytes_ = 0; stageValid_ = true;
  return true;
}

bool BlocklistManager::stageWrite(const uint8_t* data, size_t length) {
  if (!stage_ || !stageValid_ || !data || length == 0 || stageBytes_ + length > kMaxListBytes) { stageValid_ = false; return false; }
  const size_t total = LittleFS.totalBytes();
  const size_t used = LittleFS.usedBytes();
  const size_t free = total > used ? total - used : 0;
  if (free < length + kHeadroomBytes) { stageValid_ = false; return false; }
  for (size_t i = 0; i < length; ++i) {
    record_[recordLength_++] = data[i];
    if (recordLength_ != kHashBytes) continue;
    if (hasPrevious_ && !blocklist_model::lessLittleEndian40(previous_, record_)) stageValid_ = false;
    memcpy(previous_, record_, kHashBytes); hasPrevious_ = true;
    ++stageDomains_; recordLength_ = 0;
    if (stageDomains_ > kMaxListDomains) stageValid_ = false;
  }
  if (!blocklistFilesystemLock(1000)) { stageValid_ = false; return false; }
  const size_t written = stage_.write(data, length);
  blocklistFilesystemUnlock();
  stageBytes_ += static_cast<uint32_t>(written);
  if (written != length) stageValid_ = false;
  return stageValid_;
}

bool BlocklistManager::finishStage() {
  if (stage_) stage_.close();
  const bool ok = stageValid_ && recordLength_ == 0 && stageDomains_ > 0 &&
                  (!stageExpectedSize_ || stageBytes_ == stageExpectedSize_) &&
                  (!stageExpectedDomains_ || stageDomains_ == stageExpectedDomains_);
  if (!ok) {
    if (blocklistFilesystemLock(1000)) { LittleFS.remove(kStagePath); LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock(); }
    stageValid_ = false;
    return false;
  }
  if (blocklistFilesystemLock(1000)) {
    File marker = LittleFS.open(kMarkerPath, "w");
    const String value = profileName(stageProfile_);
    const bool markerOk = marker && marker.print(value) == value.length() && marker.print('\n') == 1;
    if (marker) marker.close();
    if (!markerOk) stageValid_ = false;
    blocklistFilesystemUnlock();
  }
  if (!stageValid_ || !LittleFS.exists(kMarkerPath)) {
    if (blocklistFilesystemLock(1000)) { LittleFS.remove(kStagePath); LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock(); }
    stageValid_ = false;
    return false;
  }
  pendingProfile_ = stageProfile_;
  progress_ = 95;
  commitPending_ = true;
  return true;
}

bool BlocklistManager::commitStage() {
  if (!commitPending_ || !LittleFS.exists(kStagePath)) return false;
  if (!blocklistFilesystemLock(1000)) return false;
  const bool renamed = LittleFS.rename(kStagePath, kLivePath);
  if (renamed) {
    if (live_) live_.close();
    live_ = LittleFS.open(kLivePath, "r");
  }
  blocklistFilesystemUnlock();
  if (!renamed || !live_) return false;
  bytes_ = live_.size(); domains_ = bytes_ / kHashBytes;
  applied_ = pendingProfile_;
  selected_ = pendingProfile_;
  if (!persistConfig(selected_, applied_)) {
    setStatus("Lista aplicada; no se pudo guardar el perfil");
    return false;
  }
  if (blocklistFilesystemLock(1000)) { LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock(); }
  commitPending_ = false;
  progress_ = 100;
  setStatus(String("ok: ") + domains_ + " domains");
  return true;
}

bool BlocklistManager::beginUpload() {
  if (busy_ || uploading_) return false;
  uploading_ = startStage(Profile::Custom, 0, 0, String());
  return uploading_;
}
bool BlocklistManager::writeUpload(const uint8_t* data, size_t length) {
  return uploading_ && stageWrite(data, length);
}
bool BlocklistManager::writeStaged(const uint8_t* data, size_t length) {
  return stageWrite(data, length);
}
bool BlocklistManager::finishUpload() {
  if (!uploading_) return false;
  const bool ok = finishStage();
  // Keep the validated stage pending.  The HTTP upload callback must call
  // completeUpload only after the multipart request has fully ended, so a
  // later file part cannot activate the first file accidentally.
  if (!ok) setStatus("Lista rechazada: hashes vacíos, duplicados o desordenados");
  return ok;
}
bool BlocklistManager::completeUpload() {
  if (!commitPending_ || pendingProfile_ != Profile::Custom) return false;
  const bool ok = commitStage();
  uploading_ = false;
  if (!ok && !busy_) {
    // Leave a marker behind if rename already happened; boot recovery can
    // finish the config commit.  A still-staged file is safe to discard.
    if (LittleFS.exists(kStagePath) && blocklistFilesystemLock(1000)) {
      LittleFS.remove(kStagePath); LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock();
    }
    commitPending_ = false;
  }
  return ok;
}
void BlocklistManager::abortUpload() {
  uploading_ = false;
  commitPending_ = false;
  if (stage_) stage_.close();
  if (blocklistFilesystemLock(1000)) { LittleFS.remove(kStagePath); LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock(); }
}

bool BlocklistManager::requestProfile(Profile profile) {
  if (profile == Profile::Custom || busy_ || uploading_ || !task_) return false;
  if (!persistConfig(profile, applied_)) { setStatus("No se pudo guardar el perfil seleccionado"); return false; }
  selected_ = profile;
  if (setStateMutex(100)) { failureStatus_ = ""; releaseStateMutex(); }
  operation_ = Operation::Profile; pendingProfile_ = profile; progress_ = 0; busy_ = true;
  setStatus(String("Descargando perfil ") + profileName(profile) + "…");
  xTaskNotifyGive(task_);
  return true;
}

bool BlocklistManager::requestCheck() {
  if (selected_ == Profile::Custom || busy_ || uploading_ || !task_) return false;
  if (setStateMutex(100)) { failureStatus_ = ""; releaseStateMutex(); }
  operation_ = Operation::Refresh; progress_ = 0; busy_ = true;
  setStatus("Actualizando perfil seleccionado…"); xTaskNotifyGive(task_); return true;
}

bool BlocklistManager::requestUrl(const String& url) {
  if (!url.startsWith("https://") && !url.startsWith("http://")) return false;
  if (busy_ || uploading_ || !task_) return false;
  if (setStateMutex(100)) { failureStatus_ = ""; releaseStateMutex(); }
  requestedUrl_ = url; operation_ = Operation::Url; progress_ = 0; busy_ = true;
  setStatus("Descargando URL personalizada…"); xTaskNotifyGive(task_); return true;
}

bool BlocklistManager::fetchManifest(Profile profile, Asset& asset) {
  WiFiClientSecure client; client.setCACert(GITHUB_ROOTS); client.setTimeout(8);
  HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(10000); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setRedirectLimit(5);
  if (!http.begin(client, kManifestUrl) || http.GET() != HTTP_CODE_OK) { http.end(); return false; }
  const int length = http.getSize();
  if (length > 12288) { http.end(); return false; }
  BoundedTextSink sink(12288);
  const int copied = http.writeToStream(&sink);
  String body = sink.value(); http.end();
  if (copied <= 0 || body.length() == 0 || body.length() > 12288 ||
      (length >= 0 && copied != length)) return false;
  DynamicJsonDocument doc(12288);
  if (deserializeJson(doc, body) || !doc.is<JsonObject>()) return false;
  const JsonObject root = doc.as<JsonObject>();
  const char* generatedAt = root["generatedAt"] | "";
  if (!root["schema"].is<unsigned>() || root["schema"].as<unsigned>() != 1 ||
      String(root["repository"] | "") != "felipedelpozo/adblock" || !generatedAt || !*generatedAt) return false;
  const JsonObject item = root["profiles"][profileName(profile)].as<JsonObject>();
  if (item.isNull()) return false;
  asset.url = item["url"] | ""; asset.sha256 = item["sha256"] | "";
  asset.size = item["size"] | 0; asset.domains = item["domains"] | 0;
  const String expectedAsset = String("blocklist-") + profileName(profile) + "-" + asset.sha256 + ".bin";
  const JsonObject source = item["source"].as<JsonObject>();
  return item["size"].is<uint32_t>() && item["domains"].is<uint32_t>() && asset.size > 0 && asset.domains > 0 &&
         asset.size == asset.domains * kHashBytes &&
         canonicalAssetUrl(asset.url, profile, asset.sha256) && String(item["asset"] | "") == expectedAsset &&
         !source.isNull() && String(source["name"] | "").length() && String(source["url"] | "").startsWith("https://") &&
         String(source["license"] | "").length();
}

bool BlocklistManager::downloadAsset(const Asset& asset, Profile profile) {
  if (!startStage(profile, asset.size, asset.domains, asset.sha256)) return false;
  WiFiClientSecure client; client.setCACert(GITHUB_ROOTS); client.setTimeout(8);
  HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(10000); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setRedirectLimit(5);
  if (!http.begin(client, asset.url) || http.GET() != HTTP_CODE_OK) { http.end(); if (stage_) stage_.close(); return false; }
  const int length = http.getSize();
  if (length >= 0 && static_cast<uint32_t>(length) != asset.size) { http.end(); if (stage_) stage_.close(); return false; }
  mbedtls_sha256_context digest; mbedtls_sha256_init(&digest); mbedtls_sha256_starts_ret(&digest, 0);
  DownloadSink sink(*this, asset.size, millis() + 120000, &digest);
  const int copied = http.writeToStream(&sink);
  uint8_t actual[32] = {}; mbedtls_sha256_finish_ret(&digest, actual); mbedtls_sha256_free(&digest); http.end();
  char hex[65]; for (size_t i = 0; i < 32; ++i) snprintf(hex + i * 2, 3, "%02x", actual[i]); hex[64] = '\0';
  progress_ = 90;
  if (copied <= 0 || sink.total() != asset.size || String(hex) != asset.sha256) stageValid_ = false;
  return finishStage();
}

bool BlocklistManager::downloadUrl(const String& url) {
  if (!startStage(Profile::Custom, 0, 0, String())) return false;
  WiFiClientSecure secure; WiFiClient plain;
  HTTPClient http; http.setConnectTimeout(8000); http.setTimeout(10000); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS); http.setRedirectLimit(5);
  const bool https = url.startsWith("https://");
  if (https) secure.setCACert(GITHUB_ROOTS);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url)) || http.GET() != HTTP_CODE_OK) { http.end(); if (stage_) stage_.close(); return false; }
  const int length = http.getSize(); if (length > static_cast<int>(kMaxListBytes)) { http.end(); if (stage_) stage_.close(); return false; }
  const size_t limit = length > 0 ? static_cast<size_t>(length) : kMaxListBytes;
  DownloadSink sink(*this, limit, millis() + 120000);
  const int copied = http.writeToStream(&sink);
  http.end(); if (copied <= 0 || (length >= 0 && sink.total() != static_cast<size_t>(length))) stageValid_ = false;
  progress_ = 90;
  return finishStage();
}

bool BlocklistManager::fetchProfile(Profile profile) {
  if (WiFi.status() != WL_CONNECTED) { setFailure("WiFi no conectado"); return false; }
  const uint32_t deadline = millis() + 15000;
  while (time(nullptr) < 1700000000 && !expired(deadline)) delay(100);
  if (time(nullptr) < 1700000000) { setFailure("Hora TLS no disponible"); return false; }
  Asset asset;
  if (!fetchManifest(profile, asset)) { setFailure("Manifest inválido o no disponible"); return false; }
  if (!downloadAsset(asset, profile)) {
    bool detailed = false;
    if (setStateMutex(100)) { detailed = failureStatus_.length() > 0; releaseStateMutex(); }
    if (!detailed) setFailure("Descarga, SHA-256 o lista ordenada inválida");
    return false;
  }
  return true;
}
bool BlocklistManager::fetchUrl(const String& url) {
  if (WiFi.status() != WL_CONNECTED) { setFailure("WiFi no conectado"); return false; }
  if (url.startsWith("https://")) {
    const uint32_t deadline = millis() + 15000;
    while (time(nullptr) < 1700000000 && !expired(deadline)) delay(100);
    if (time(nullptr) < 1700000000) { setFailure("Hora TLS no disponible"); return false; }
  }
  if (!downloadUrl(url)) { setFailure("URL no disponible o lista inválida"); return false; }
  return true;
}

void BlocklistManager::worker() {
  const Operation op = operation_;
  bool ok = false;
  if (op == Operation::Profile || op == Operation::Refresh) ok = fetchProfile(selected_);
  else if (op == Operation::Url) ok = fetchUrl(requestedUrl_);
  if (!ok) {
    if (stage_) stage_.close();
    if (blocklistFilesystemLock(1000)) { LittleFS.remove(kStagePath); LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock(); }
    bool detailed = false;
    if (setStateMutex(100)) { detailed = failureStatus_.length() > 0; releaseStateMutex(); }
    if (!detailed) setStatus("No se pudo validar la lista; se conserva la activa");
    progress_ = 0;
  }
  // Keep the operation busy until poll() performs the short main-thread
  // rename.  This prevents a second request from deleting a validated stage.
  if (!ok) busy_ = false;
}

void BlocklistManager::taskEntry(void* arg) {
  BlocklistManager* self = static_cast<BlocklistManager*>(arg);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    self->worker();
  }
}

void BlocklistManager::poll() {
  // Manual staging is activated only by the completed HTTP request.
  if (commitPending_ && busy_) {
    const Profile profile = pendingProfile_;
    if (commitStage()) {
      if (profile == Profile::Custom) selected_ = applied_ = Profile::Custom;
      busy_ = false;
    } else if (busy_) {
      // A failed rename/config write must not leave the UI permanently busy.
      commitPending_ = false;
      const bool unapplied = LittleFS.exists(kStagePath);
      if (unapplied && blocklistFilesystemLock(1000)) {
        LittleFS.remove(kStagePath); LittleFS.remove(kMarkerPath); blocklistFilesystemUnlock();
      }
      busy_ = false;
      if (unapplied) setStatus("No se pudo aplicar la lista; se conserva la activa");
      else { progress_ = 100; setStatus("Lista aplicada; no se pudo guardar el perfil"); }
    }
  }
}
