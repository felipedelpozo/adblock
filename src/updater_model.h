#pragma once

#include <stdint.h>
#include <string>
#include <cstring>
#include <cstdio>

namespace updater_model {

inline bool strictSemver(const std::string& value) {
  if (value.empty()) return false;
  size_t i = 0;
  if (value[i] == 'v') ++i;
  int parts = 0;
  for (; parts < 3; ++parts) {
    if (i >= value.size() || value[i] < '0' || value[i] > '9') return false;
    if (value[i] == '0' && i + 1 < value.size() && value[i + 1] != '.') return false;
    unsigned digits = 0;
    while (i < value.size() && value[i] >= '0' && value[i] <= '9') {
      ++i;
      if (++digits > 6) return false;
    }
    if (parts < 2) { if (i >= value.size() || value[i++] != '.') return false; }
  }
  return i == value.size();
}

inline int compareSemver(const std::string& left, const std::string& right) {
  auto part = [](const std::string& value, size_t& at) {
    if (at < value.size() && value[at] == 'v') ++at;
    int n = 0; while (at < value.size() && value[at] != '.') n = n * 10 + value[at++] - '0';
    if (at < value.size()) ++at; return n;
  };
  size_t a = 0, b = 0;
  for (int i = 0; i < 3; ++i) { int x = part(left, a), y = part(right, b); if (x != y) return x < y ? -1 : 1; }
  return 0;
}

inline bool strictSha256(const std::string& value) {
  if (value.size() != 64) return false;
  for (char c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

inline bool canonicalAssetUrl(const std::string& url, const std::string& tag, const std::string& asset) {
  return url == "https://github.com/felipedelpozo/adblock/releases/download/" + tag + "/" + asset;
}

inline bool digestMatches(const std::string& expected, const uint8_t* actual) {
  if (!strictSha256(expected)) return false;
  const auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  uint8_t different = 0;
  for (unsigned i = 0; i < 32; ++i) {
    different |= static_cast<uint8_t>(((hex(expected[i * 2]) << 4) | hex(expected[i * 2 + 1])) ^ actual[i]);
  }
  return different == 0;
}

// Match the compiled identity even when the literal crosses download chunks.
// Fixed memory and linear processing keep this out of the DNS allocation path.
class IdentityMatcher {
 public:
  IdentityMatcher(const char* profile, const char* version) {
    const int count = std::snprintf(pattern_, sizeof(pattern_), "ADBLOCK_ID:%s:%s", profile, version);
    if (count < 0 || static_cast<size_t>(count) >= sizeof(pattern_)) return;
    length_ = static_cast<size_t>(count) + 1;  // Include the terminating NUL.
    for (size_t i = 1, matched = 0; i < length_; ++i) {
      while (matched && pattern_[i] != pattern_[matched]) matched = prefix_[matched - 1];
      if (pattern_[i] == pattern_[matched]) ++matched;
      prefix_[i] = static_cast<uint8_t>(matched);
    }
  }
  void write(const uint8_t* data, size_t size) {
    if (!length_ || found_) return;
    for (size_t i = 0; i < size; ++i) {
      while (matched_ && data[i] != static_cast<uint8_t>(pattern_[matched_])) matched_ = prefix_[matched_ - 1];
      if (data[i] == static_cast<uint8_t>(pattern_[matched_])) ++matched_;
      if (matched_ == length_) { found_ = true; return; }
    }
  }
  bool found() const { return found_; }
 private:
  char pattern_[96] = {};
  uint8_t prefix_[96] = {};
  size_t length_ = 0, matched_ = 0;
  bool found_ = false;
};

}  // namespace updater_model
