#pragma once

#include <Arduino.h>
#include <atomic>

// A single coordinator protects the inactive OTA slot from browser and
// ArduinoOTA uploads while a GitHub release is being streamed.
bool firmwareUpdateTryLock(uint32_t timeoutMs);
void firmwareUpdateUnlock();

class GithubUpdater {
 public:
  bool begin();
  bool requestCheck();
  bool requestInstall(const String& expectedVersion);
  bool busy() const;
  bool canInstall() const;
  uint8_t progress() const;
  String status() const;
  String availableVersion() const;
  String nonce() const;

 private:
  static void taskEntry(void* arg);
  void runCheck();
  void runInstall();
  void setStatus(const String& value, const String& available = String());

  void* task_ = nullptr;
  void* stateMutex_ = nullptr;
  std::atomic<bool> busy_{false};
  std::atomic<uint8_t> progress_{0};
  uint8_t operation_ = 0;
  String checkedUrl_;
  String checkedSha256_;
  size_t checkedSize_ = 0;
  String status_ = "Nunca comprobado";
  String availableVersion_;
};

extern GithubUpdater githubUpdater;
