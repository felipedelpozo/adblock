#include "github_updater.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include "firmware_identity.h"
#include "updater_model.h"
#include "github_manifest.h"
#include "github_roots.h"

namespace {


SemaphoreHandle_t firmwareMutex = nullptr;

bool expired(uint32_t deadline) {
  return static_cast<int32_t>(millis() - deadline) >= 0;
}

class BoundedStream final : public Stream {
 public:
  BoundedStream(size_t limit, uint32_t deadline) : limit_(limit), deadline_(deadline) {
    value_.reserve(limit);
  }
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (expired(deadline_) || length > limit_ - value_.length() ||
        !value_.concat(reinterpret_cast<const char*>(data), length)) {
      failed_ = true;
      return 0;
    }
    delay(1);
    return length;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  const String& value() const { return value_; }
  bool failed() const { return failed_; }
 private:
  String value_;
  size_t limit_;
  uint32_t deadline_;
  bool failed_ = false;
};

class FirmwareStream final : public Stream {
 public:
  FirmwareStream(mbedtls_sha256_context& digest, size_t expected, uint32_t deadline,
                 const char* version, std::atomic<uint8_t>& progress)
      : digest_(digest), expected_(expected), deadline_(deadline),
        identity_(firmware_identity::PROFILE, version), progress_(progress) {}
  size_t write(uint8_t byte) override { return write(&byte, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (failed_ || expired(deadline_) || length > expected_ - total_) {
      failed_ = true;
      return 0;
    }
    // Arduino-ESP32 2.x declares this input mutable but only copies it into
    // its own staging buffer; Stream's contract supplies a const input.
    if (Update.write(const_cast<uint8_t*>(data), length) != length ||
        mbedtls_sha256_update_ret(&digest_, data, length) != 0) {
      failed_ = true;
      return 0;
    }
    identity_.write(data, length);
    total_ += length;
    progress_ = static_cast<uint8_t>((total_ * 90ULL) / expected_);
    delay(1);  // Give the C3's main loop a scheduling window between flash writes.
    return length;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  size_t total() const { return total_; }
  bool failed() const { return failed_; }
  bool identityFound() const { return identity_.found(); }
 private:
  mbedtls_sha256_context& digest_;
  size_t expected_;
  uint32_t deadline_;
  updater_model::IdentityMatcher identity_;
  std::atomic<uint8_t>& progress_;
  size_t total_ = 0;
  bool failed_ = false;
};

void configureHttp(HTTPClient& http, WiFiClientSecure& client) {
  client.setCACert(GITHUB_ROOTS);
  client.setTimeout(8);
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(5);
  http.setReuse(false);
}

bool fetchText(const String& url, String& result, size_t limit, int& code) {
  code = 0;
  if (!url.startsWith("https://") || WiFi.status() != WL_CONNECTED) return false;
  const uint32_t deadline = millis() + 30000;
  WiFiClientSecure client;
  HTTPClient http;
  configureHttp(http, client);
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept", "application/vnd.github+json");
  code = http.GET();
  const int length = http.getSize();
  if (code != HTTP_CODE_OK || (length >= 0 && static_cast<size_t>(length) > limit)) {
    http.end();
    return false;
  }
  BoundedStream sink(limit, deadline);
  const int copied = http.writeToStream(&sink);  // HTTPClient decodes chunked responses.
  result = sink.value();
  http.end();
  return copied > 0 && !sink.failed() && result.length() == static_cast<size_t>(copied) &&
         (length < 0 || copied == length);
}

bool waitForClock() {
  const uint32_t started = millis();
  while (time(nullptr) < 1700000000 && millis() - started < 15000) delay(100);
  return time(nullptr) >= 1700000000;
}

}  // namespace

bool firmwareUpdateTryLock(uint32_t timeoutMs) {
  return firmwareMutex && xSemaphoreTake(firmwareMutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}
void firmwareUpdateUnlock() {
  if (firmwareMutex) xSemaphoreGive(firmwareMutex);
}
GithubUpdater githubUpdater;

bool GithubUpdater::begin() {
  if (!firmwareMutex) firmwareMutex = xSemaphoreCreateMutex();
  if (!stateMutex_) stateMutex_ = xSemaphoreCreateMutex();
  if (!firmwareMutex || !stateMutex_) {
    setStatus("Coordinador OTA no disponible");
    return false;
  }
  if (task_) return true;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  if (xTaskCreatePinnedToCore(taskEntry, "github_ota", 8192, this, 1,
                            reinterpret_cast<TaskHandle_t*>(&task_), 0) != pdPASS) {
    task_ = nullptr;
    setStatus("No se pudo iniciar el updater");
    return false;
  }
  return true;
}

bool GithubUpdater::requestCheck() {
  bool idle = false;
  if (!task_ || !busy_.compare_exchange_strong(idle, true)) return false;
  progress_ = 0;
  operation_ = 1;
  xTaskNotifyGive(reinterpret_cast<TaskHandle_t>(task_));
  return true;
}

bool GithubUpdater::requestInstall(const String& expectedVersion) {
  bool idle = false;
  if (!task_ || !busy_.compare_exchange_strong(idle, true)) return false;
  const auto mutex = reinterpret_cast<SemaphoreHandle_t>(stateMutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    busy_ = false;
    return false;
  }
  const bool ready = checkedSize_ > 0 && expectedVersion == availableVersion_ &&
                     firmware_identity::runtimeCompatible();
  xSemaphoreGive(mutex);
  if (!ready) { busy_ = false; return false; }
  progress_ = 0;
  operation_ = 2;
  xTaskNotifyGive(reinterpret_cast<TaskHandle_t>(task_));
  return true;
}

bool GithubUpdater::busy() const { return busy_.load(); }
uint8_t GithubUpdater::progress() const { return progress_.load(); }

bool GithubUpdater::canInstall() const {
  if (!stateMutex_ || busy_) return false;
  const auto mutex = reinterpret_cast<SemaphoreHandle_t>(stateMutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
  const bool ready = checkedSize_ > 0 && availableVersion_.length() > 0;
  xSemaphoreGive(mutex);
  return ready;
}

String GithubUpdater::status() const {
  if (!stateMutex_) return status_;
  const auto mutex = reinterpret_cast<SemaphoreHandle_t>(stateMutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) return "Estado ocupado";
  const String value = status_;
  xSemaphoreGive(mutex);
  return value;
}

String GithubUpdater::availableVersion() const {
  if (!stateMutex_) return availableVersion_;
  const auto mutex = reinterpret_cast<SemaphoreHandle_t>(stateMutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) return String();
  const String value = availableVersion_;
  xSemaphoreGive(mutex);
  return value;
}

String GithubUpdater::nonce() const {
  char value[33];
  snprintf(value, sizeof(value), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return String(value);
}

void GithubUpdater::setStatus(const String& value, const String& available) {
  const auto mutex = reinterpret_cast<SemaphoreHandle_t>(stateMutex_);
  if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) return;
  status_ = value;
  if (available.length()) availableVersion_ = available;
  if (mutex) xSemaphoreGive(mutex);
}

void GithubUpdater::taskEntry(void* arg) {
  auto* self = static_cast<GithubUpdater*>(arg);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (self->operation_ == 1) self->runCheck();
    else if (self->operation_ == 2) self->runInstall();
    self->busy_ = false;
    Serial.printf("[github-ota] status=%s heap=%lu stack_min=%lu\n", self->status().c_str(),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
  }
}

void GithubUpdater::runCheck() {
  const auto mutex = reinterpret_cast<SemaphoreHandle_t>(stateMutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    setStatus("Estado ocupado");
    return;
  }
  // A failed new check must never leave a stale release installable.
  checkedSize_ = 0;
  availableVersion_ = "";
  xSemaphoreGive(mutex);
  setStatus("Comprobando releases de GitHub…");
  if (!firmware_identity::runtimeCompatible()) { setStatus("Hardware incompatible"); return; }
  if (!waitForClock()) { setStatus("Hora no sincronizada; no se puede validar TLS"); return; }
  String release;
  int code = 0;
  if (!fetchText("https://api.github.com/repos/felipedelpozo/adblock/releases/latest", release, 32768, code)) {
    setStatus(code == HTTP_CODE_NOT_FOUND ? "No hay releases publicadas" :
              (code == 403 || code == 429 ? "GitHub limita las consultas; inténtalo más tarde" :
               (code > 0 ? "GitHub HTTP " + String(code) : "No se pudo validar TLS o conectar a GitHub")));
    return;
  }
  std::string tag, manifestUrl;
  if (!github_manifest::parseRelease(std::string(release.c_str()), tag, manifestUrl)) {
    setStatus("Release sin manifest compatible");
    return;
  }
  release = "";
  String manifest;
  if (!fetchText(String(manifestUrl.c_str()), manifest, 8192, code)) {
    setStatus("Manifest inaccesible");
    return;
  }
  github_manifest::Build build;
  if (!github_manifest::parseBuild(std::string(manifest.c_str()), tag, firmware_identity::PROFILE,
                                   firmware_identity::CHIP, firmware_identity::BOARD, build)) {
    setStatus("Manifest incompatible con este perfil");
    return;
  }
  if (updater_model::compareSemver(build.version, firmware_identity::VERSION) <= 0) {
    setStatus("La versión instalada está al día (" + String(firmware_identity::VERSION) + ")",
              String(build.version.c_str()));
    return;
  }
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot || build.size > slot->size) { setStatus("Firmware demasiado grande para el slot OTA"); return; }
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) { setStatus("Estado ocupado"); return; }
  checkedUrl_ = build.url.c_str();
  checkedSha256_ = build.sha256.c_str();
  checkedSize_ = build.size;
  availableVersion_ = build.version.c_str();
  xSemaphoreGive(mutex);
  setStatus("Release lista para instalar: " + String(build.version.c_str()));
}

void GithubUpdater::runInstall() {
  setStatus("Descargando firmware…");
  if (!waitForClock()) { setStatus("Hora no sincronizada; no se puede validar TLS"); return; }
  if (!firmwareUpdateTryLock(250)) { setStatus("Otra actualización OTA está en curso"); return; }
  const auto mutex = reinterpret_cast<SemaphoreHandle_t>(stateMutex_);
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    firmwareUpdateUnlock();
    setStatus("Estado ocupado");
    return;
  }
  const String url = checkedUrl_, digestHex = checkedSha256_, version = availableVersion_;
  const size_t expected = checkedSize_;
  xSemaphoreGive(mutex);
  const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
  if (!slot || expected == 0 || expected > slot->size) {
    firmwareUpdateUnlock();
    setStatus("Firmware demasiado grande para el slot OTA");
    return;
  }
  const uint32_t deadline = millis() + 120000;
  WiFiClientSecure client;
  HTTPClient http;
  configureHttp(http, client);
  if (!http.begin(client, url)) { firmwareUpdateUnlock(); setStatus("No se pudo abrir el firmware"); return; }
  const int code = http.GET(), length = http.getSize();
  if (code != HTTP_CODE_OK || (length >= 0 && static_cast<size_t>(length) != expected)) {
    http.end();
    firmwareUpdateUnlock();
    setStatus("Tamaño o respuesta del firmware no coincide");
    return;
  }
  if (!Update.begin(expected, U_FLASH)) {
    http.end();
    firmwareUpdateUnlock();
    setStatus("No hay espacio para el firmware");
    return;
  }
  mbedtls_sha256_context digest;
  mbedtls_sha256_init(&digest);
  const int started = mbedtls_sha256_starts_ret(&digest, 0);
  FirmwareStream sink(digest, expected, deadline, version.c_str(), progress_);
  const int copied = started == 0 ? http.writeToStream(&sink) : -1;
  uint8_t actual[32];
  const int finished = mbedtls_sha256_finish_ret(&digest, actual);
  mbedtls_sha256_free(&digest);
  http.end();
  const char* error = nullptr;
  if (copied != static_cast<int>(expected) || sink.failed() || sink.total() != expected) error = "Descarga incompleta";
  else if (started || finished || !updater_model::digestMatches(digestHex.c_str(), actual)) error = "SHA-256 del firmware no coincide";
  else if (!sink.identityFound()) error = "El binario no corresponde a este perfil o versión";
  else if (!Update.end()) error = "La activación OTA falló";
  if (error) {
    Update.abort();
    firmwareUpdateUnlock();
    progress_ = 0;
    setStatus(error);
    return;
  }
  // Retain the OTA lock until reset; a concurrent upload cannot change the
  // selected boot partition after verification has succeeded.
  progress_ = 100;
  setStatus("Firmware verificado; reiniciando en " + version);
  delay(500);
  ESP.restart();
}
