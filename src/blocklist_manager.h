#pragma once

#include <Arduino.h>
#include <FS.h>
#include <atomic>

// Filesystem access is shared with the DNS reader and the allowlist writer.
// The lock is intentionally short-lived; network/TLS work never holds it.
bool blocklistFilesystemLock(uint32_t timeoutMs);
void blocklistFilesystemUnlock();

class BlocklistManager {
 public:
  enum class Profile : uint8_t { Custom = 0, Light, Balanced, Strict };

  bool begin();
  void poll();

  bool contains(uint64_t hash) const;
  uint32_t domains() const { return domains_; }
  uint32_t bytes() const { return bytes_; }

  bool beginUpload();
  bool writeUpload(const uint8_t* data, size_t length);
  bool writeStaged(const uint8_t* data, size_t length);
  bool finishUpload();
  bool completeUpload();
  void abortUpload();

  bool requestProfile(Profile profile);
  bool requestCheck();
  bool requestUrl(const String& url);

  Profile selectedProfile() const;
  Profile appliedProfile() const;
  String selectedName() const;
  String appliedName() const;
  String status() const;
  bool busy() const { return busy_ || uploading_; }
  uint8_t progress() const { return progress_; }

  static const char* profileName(Profile profile);
  static bool profileFromName(const char* value, Profile& profile);

 private:
  enum class Operation : uint8_t { None = 0, Profile, Refresh, Url };
  struct Asset {
    String url;
    String sha256;
    uint32_t size = 0;
    uint32_t domains = 0;
  };

  static void taskEntry(void* arg);
  void worker();
  bool fetchProfile(Profile profile);
  bool fetchUrl(const String& url);
  bool fetchManifest(Profile profile, Asset& asset);
  bool downloadAsset(const Asset& asset, Profile profile);
  bool downloadUrl(const String& url);

  bool startStage(Profile profile, uint32_t expectedSize, uint32_t expectedDomains,
                  const String& expectedSha256);
  bool stageWrite(const uint8_t* data, size_t length);
  bool finishStage();
  bool validateFile(const char* path, uint32_t& domains, uint32_t& bytes) const;
  bool commitStage();
  bool recoverStage();
  bool persistConfig(Profile selected, Profile applied);
  void loadConfig();
  void setStatus(const String& value);
  void setFailure(const String& value);
  bool setStateMutex(uint32_t timeoutMs) const;
  void releaseStateMutex() const;
  bool hasSpace(uint32_t newBytes) const;
  static bool validSha256(const String& value);
  static bool canonicalAssetUrl(const String& url, Profile profile, const String& sha256);

  mutable File live_;
  uint32_t domains_ = 0;
  uint32_t bytes_ = 0;
  Profile selected_ = Profile::Custom;
  Profile applied_ = Profile::Custom;

  File stage_;
  Profile stageProfile_ = Profile::Custom;
  uint32_t stageExpectedSize_ = 0;
  uint32_t stageExpectedDomains_ = 0;
  String stageExpectedSha256_;
  uint8_t record_[5] = {};
  uint8_t recordLength_ = 0;
  uint8_t previous_[5] = {};
  bool hasPrevious_ = false;
  uint32_t stageDomains_ = 0;
  uint32_t stageBytes_ = 0;
  bool stageValid_ = false;
  bool uploading_ = false;
  std::atomic<bool> commitPending_{false};
  Profile pendingProfile_ = Profile::Custom;

  SemaphoreHandle_t stateMutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  std::atomic<bool> busy_{false};
  std::atomic<uint8_t> progress_{0};
  std::atomic<Operation> operation_{Operation::None};
  String requestedUrl_;
  String status_ = "Nunca comprobado";
  String failureStatus_;
};

extern BlocklistManager blocklistManager;
