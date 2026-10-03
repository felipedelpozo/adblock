#pragma once

#include <stddef.h>
#include <stdint.h>

namespace blocklist_model {

static const size_t kHashBytes = 5;

inline bool lessLittleEndian40(const uint8_t* left, const uint8_t* right) {
  for (int index = static_cast<int>(kHashBytes) - 1; index >= 0; --index) {
    if (left[index] != right[index]) return left[index] < right[index];
  }
  return false;
}

inline bool validSortedUnique(const uint8_t* data, size_t length) {
  if (!data || length == 0 || length % kHashBytes != 0) return false;
  const size_t records = length / kHashBytes;
  for (size_t index = 1; index < records; ++index) {
    if (!lessLittleEndian40(data + (index - 1) * kHashBytes, data + index * kHashBytes)) return false;
  }
  return true;
}

}  // namespace blocklist_model
