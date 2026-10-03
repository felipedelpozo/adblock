#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// A deliberately small domain-rule table.  It stores only canonical ASCII
// host names and performs suffix matching without heap allocation.
namespace domain_rules {

static const size_t kMaxRules = 200;
static const size_t kMaxDomainLength = 253;

inline uint64_t fnv40(const char* value, size_t length) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < length; ++i) {
    hash ^= static_cast<uint8_t>(value[i]);
    hash *= 0x100000001b3ULL;
  }
  return hash & ((1ULL << 40) - 1);
}

// Canonicalise a host name.  This intentionally accepts plain DNS names only:
// no hosts-file prefixes, ABP operators, wildcards, paths, ports or labels
// containing '_' are accepted.  A final root dot is harmless and removed.
inline bool normalize(const char* input, char* output, size_t capacity) {
  if (!input || !output || capacity < 2) return false;
  size_t begin = 0;
  size_t end = strlen(input);
  while (begin < end && (input[begin] == ' ' || input[begin] == '\t' ||
                         input[begin] == '\r' || input[begin] == '\n')) ++begin;
  while (end > begin && (input[end - 1] == ' ' || input[end - 1] == '\t' ||
                         input[end - 1] == '\r' || input[end - 1] == '\n')) --end;
  if (end > begin && input[end - 1] == '.') --end;
  const size_t length = end - begin;
  if (length == 0 || length > kMaxDomainLength || length + 1 > capacity) return false;

  size_t out = 0;
  size_t labelLength = 0;
  bool hasDot = false;
  bool labelStart = true;
  for (size_t i = begin; i < end; ++i) {
    const unsigned char ch = static_cast<unsigned char>(input[i]);
    if (ch == '.') {
      if (labelLength == 0 || labelLength > 63 || labelStart) return false;
      if (output[out - 1] == '-') return false;
      output[out++] = '.';
      labelLength = 0;
      labelStart = true;
      hasDot = true;
      continue;
    }
    if (ch >= 'A' && ch <= 'Z') {
      output[out++] = static_cast<char>(ch - 'A' + 'a');
    } else if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-') {
      output[out++] = static_cast<char>(ch);
    } else {
      return false;
    }
    if (labelStart && output[out - 1] == '-') return false;
    labelStart = false;
    ++labelLength;
    if (labelLength > 63) return false;
  }
  if (labelLength == 0 || output[out - 1] == '-') return false;
  if (!hasDot) return false;
  output[out] = '\0';
  return true;
}

class Table {
 public:
  Table() : count_(0) {}

  void clear() { count_ = 0; }
  size_t size() const { return count_; }
  const char* at(size_t index) const { return index < count_ ? domains_[index] : ""; }

  bool add(const char* value) {
    char canonical[kMaxDomainLength + 1];
    if (!normalize(value, canonical, sizeof(canonical))) return false;
    return addCanonical(canonical);
  }

  bool addCanonical(const char* canonical) {
    if (!canonical || !*canonical || count_ >= kMaxRules) return false;
    const size_t length = strlen(canonical);
    if (length > kMaxDomainLength) return false;
    size_t at = 0;
    while (at < count_ && strcmp(domains_[at], canonical) < 0) ++at;
    if (at < count_ && strcmp(domains_[at], canonical) == 0) return false;
    for (size_t i = count_; i > at; --i) strcpy(domains_[i], domains_[i - 1]);
    strcpy(domains_[at], canonical);
    ++count_;
    return true;
  }

  bool remove(const char* value) {
    char canonical[kMaxDomainLength + 1];
    if (!normalize(value, canonical, sizeof(canonical))) return false;
    for (size_t i = 0; i < count_; ++i) {
      if (strcmp(domains_[i], canonical) != 0) continue;
      for (size_t j = i + 1; j < count_; ++j) strcpy(domains_[j - 1], domains_[j]);
      --count_;
      domains_[count_][0] = '\0';
      return true;
    }
    return false;
  }

  bool containsExact(const char* canonical) const {
    size_t lo = 0, hi = count_;
    while (lo < hi) {
      const size_t mid = lo + (hi - lo) / 2;
      const int comparison = strcmp(domains_[mid], canonical);
      if (comparison < 0) lo = mid + 1;
      else if (comparison > 0) hi = mid;
      else return true;
    }
    return false;
  }

  bool containsSuffix(const char* value) const {
    char canonical[kMaxDomainLength + 1];
    if (!normalize(value, canonical, sizeof(canonical))) return false;
    size_t start = 0;
    while (start < strlen(canonical)) {
      if (containsExact(canonical + start)) return true;
      const char* dot = strchr(canonical + start, '.');
      if (!dot) break;
      start = static_cast<size_t>(dot - canonical) + 1;
    }
    return false;
  }

 private:
  char domains_[kMaxRules][kMaxDomainLength + 1];
  size_t count_;
};

}  // namespace domain_rules
