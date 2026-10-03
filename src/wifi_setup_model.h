#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

// Platform-independent contracts for Wi-Fi provisioning.  Keep this header
// free of Arduino types so malformed input and persistence records can be
// tested on the native PlatformIO environment.
namespace wifi_setup_model {

constexpr std::size_t kMaxSsidBytes = 32;
constexpr std::size_t kMaxPasswordBytes = 63;
constexpr std::size_t kRecordSize = 4 + 1 + 1 + 1 + 1 + kMaxSsidBytes + kMaxPasswordBytes + 4;

struct Credentials {
  std::string ssid;
  std::string password;
};

// Wi-Fi names and passwords are byte strings.  NUL and ASCII controls are
// rejected; all other UTF-8 bytes are preserved, including surrounding
// spaces.  The ESP32 Wi-Fi stack accepts the same byte-oriented values.
inline bool validText(const std::string& value, std::size_t maxBytes) {
  if (value.empty() || value.size() > maxBytes) return false;
  for (const unsigned char byte : value) {
    if (byte == 0 || byte < 0x20 || byte == 0x7f) return false;
  }
  return true;
}

inline bool validSsid(const std::string& ssid) {
  return validText(ssid, kMaxSsidBytes);
}

inline bool validPassword(const std::string& password) {
  return password.empty() || (validText(password, kMaxPasswordBytes) && password.size() >= 8);
}

inline bool validCredentials(const Credentials& credentials) {
  return validSsid(credentials.ssid) && validPassword(credentials.password);
}

// The record is a fixed-size, versioned value suitable for one Preferences
// blob.  Unused bytes are zeroed and included in the checksum, which makes a
// truncated or stale value fail closed.
namespace detail {

constexpr std::uint8_t kVersion = 1;
constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kSsidOffset = kHeaderSize;
constexpr std::size_t kPasswordOffset = kSsidOffset + kMaxSsidBytes;
constexpr std::size_t kChecksumOffset = kPasswordOffset + kMaxPasswordBytes;

inline std::uint32_t checksum(const std::uint8_t* data, std::size_t size) {
  std::uint32_t value = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) {
    value ^= data[i];
    value *= 16777619u;
  }
  return value;
}

inline std::uint32_t readLe32(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0]) |
      (static_cast<std::uint32_t>(data[1]) << 8) |
      (static_cast<std::uint32_t>(data[2]) << 16) |
      (static_cast<std::uint32_t>(data[3]) << 24);
}

inline void writeLe32(std::uint8_t* data, std::uint32_t value) {
  data[0] = static_cast<std::uint8_t>(value);
  data[1] = static_cast<std::uint8_t>(value >> 8);
  data[2] = static_cast<std::uint8_t>(value >> 16);
  data[3] = static_cast<std::uint8_t>(value >> 24);
}

}  // namespace detail

inline std::size_t encodeRecord(const Credentials& credentials, std::uint8_t* out, std::size_t outSize) {
  if (!out || outSize < kRecordSize || !validCredentials(credentials)) return 0;
  std::memset(out, 0, kRecordSize);
  out[0] = 'W'; out[1] = 'F'; out[2] = 'C'; out[3] = '1';
  out[4] = detail::kVersion;
  out[5] = static_cast<std::uint8_t>(credentials.ssid.size());
  out[6] = static_cast<std::uint8_t>(credentials.password.size());
  std::memcpy(out + detail::kSsidOffset, credentials.ssid.data(), credentials.ssid.size());
  std::memcpy(out + detail::kPasswordOffset, credentials.password.data(), credentials.password.size());
  detail::writeLe32(out + detail::kChecksumOffset, detail::checksum(out, detail::kChecksumOffset));
  return kRecordSize;
}

inline bool validRecord(const std::uint8_t* data, std::size_t size) {
  if (!data || size != kRecordSize || data[0] != 'W' || data[1] != 'F' ||
      data[2] != 'C' || data[3] != '1' || data[4] != detail::kVersion ||
      data[5] == 0 || data[5] > kMaxSsidBytes || data[6] > kMaxPasswordBytes ||
      (data[6] > 0 && data[6] < 8)) return false;
  for (std::size_t i = data[5]; i < kMaxSsidBytes; ++i) {
    if (data[detail::kSsidOffset + i] != 0) return false;
  }
  for (std::size_t i = data[6]; i < kMaxPasswordBytes; ++i) {
    if (data[detail::kPasswordOffset + i] != 0) return false;
  }
  if (detail::readLe32(data + detail::kChecksumOffset) != detail::checksum(data, detail::kChecksumOffset)) return false;
  Credentials credentials;
  credentials.ssid.assign(reinterpret_cast<const char*>(data + detail::kSsidOffset), data[5]);
  credentials.password.assign(reinterpret_cast<const char*>(data + detail::kPasswordOffset), data[6]);
  return validCredentials(credentials);
}

inline bool decodeRecord(const std::uint8_t* data, std::size_t size, Credentials& credentials) {
  if (!validRecord(data, size)) return false;
  credentials.ssid.assign(reinterpret_cast<const char*>(data + detail::kSsidOffset), data[5]);
  credentials.password.assign(reinterpret_cast<const char*>(data + detail::kPasswordOffset), data[6]);
  return true;
}

}  // namespace wifi_setup_model
