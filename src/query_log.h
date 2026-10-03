#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace blocked_log {

enum class Reason : uint8_t { Blocklist, Custom, Client };

struct Entry {
  uint32_t id;
  uint32_t uptimeSeconds;
  uint32_t client;
  uint16_t type;
  Reason reason;
  char domain[256];
};

// Fixed storage and bounded copies keep the DNS hot path allocation-free.
// History is intentionally volatile: no flash writes or browsing history on disk.
class Ring {
 public:
  static constexpr size_t kCapacity = 64;

  void record(const char* domain, uint32_t client, uint16_t type,
              Reason reason, uint32_t uptimeSeconds) {
    Entry& entry = entries_[next_];
    entry.id = ++sequence_;
    entry.uptimeSeconds = uptimeSeconds;
    entry.client = client;
    entry.type = type;
    entry.reason = reason;
    strncpy(entry.domain, domain ? domain : "(invalid DNS query)", sizeof(entry.domain) - 1);
    entry.domain[sizeof(entry.domain) - 1] = '\0';
    next_ = (next_ + 1) % kCapacity;
    if (count_ < kCapacity) ++count_;
  }

  size_t size() const { return count_; }

  const Entry* newest(size_t offset) const {
    if (offset >= count_) return nullptr;
    return &entries_[(next_ + kCapacity - 1 - offset) % kCapacity];
  }

 private:
  Entry entries_[kCapacity] = {};
  size_t next_ = 0;
  size_t count_ = 0;
  uint32_t sequence_ = 0;
};

inline unsigned char lowerAscii(unsigned char c) {
  return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

inline bool containsIgnoreCase(const char* value, const char* needle) {
  if (!*needle) return true;
  for (; *value; ++value) {
    size_t i = 0;
    while (needle[i] && value[i] && lowerAscii(value[i]) == lowerAscii(needle[i])) ++i;
    if (!needle[i]) return true;
  }
  return false;
}

// JSON remains valid even for arbitrary bytes from a DNS label. The caller
// provides seven bytes (six for a Unicode escape and a terminating null).
inline size_t escapeJsonByte(unsigned char value, char* output) {
  const char hex[] = "0123456789abcdef";
  size_t length = 1;
  if (value == '"' || value == '\\') {
    output[0] = '\\'; output[1] = value; length = 2;
  } else if (value < 0x20 || value >= 0x7f) {
    output[0] = '\\'; output[1] = 'u'; output[2] = '0'; output[3] = '0';
    output[4] = hex[value >> 4]; output[5] = hex[value & 0x0f]; length = 6;
  } else {
    output[0] = value;
  }
  output[length] = '\0';
  return length;
}

}  // namespace blocked_log
