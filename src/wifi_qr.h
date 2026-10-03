#pragma once

#include <cstddef>

namespace round_ui {

// Snapshot::ap is a 24-byte buffer, so at most 23 bytes may be part of an AP
// name.  Keeping this bound here prevents a QR payload from reading past the
// snapshot even when a malformed producer omitted the terminator.
constexpr std::size_t kSetupWifiApCapacity = 24;
constexpr std::size_t kSetupWifiApMaxBytes = kSetupWifiApCapacity - 1;

// QR version 3 with medium correction carries 42 bytes in byte mode.  The
// extra byte is for the C string terminator returned to the caller.
constexpr std::size_t kSetupWifiPayloadMaxBytes = 42;
constexpr std::size_t kSetupWifiPayloadCapacity = kSetupWifiPayloadMaxBytes + 1;

namespace detail {

inline bool wifiQrReserved(char value) {
  return value == ';' || value == ',' || value == ':' || value == '\\' || value == '"';
}

inline bool wifiQrControl(unsigned char value) {
  return value < 0x20U || value == 0x7fU;
}

}  // namespace detail

// Build an open Wi-Fi QR payload for the setup access point.  This helper is
// deliberately independent from Arduino and does not accept station
// credentials.  The caller must provide an AP buffer with the same bounded
// contract as Snapshot::ap (24 bytes including its terminator).
inline bool setupWifiPayload(bool portal, const char* ap, char* output, std::size_t capacity) {
  if (!output || capacity < 1) return false;
  output[0] = '\0';
  if (!portal || !ap) return false;

  std::size_t apLength = 0;
  bool terminated = false;
  for (; apLength < kSetupWifiApCapacity; ++apLength) {
    if (ap[apLength] == '\0') {
      terminated = true;
      break;
    }
  }
  if (!terminated || apLength == 0 || apLength > kSetupWifiApMaxBytes) return false;

  constexpr char kPrefix[] = "WIFI:T:nopass;S:";
  constexpr char kSuffix[] = ";;";
  const std::size_t prefixLength = sizeof(kPrefix) - 1;
  const std::size_t suffixLength = sizeof(kSuffix) - 1;
  std::size_t outLength = 0;
  const auto append = [&](char value) {
    if (outLength + 1 >= capacity || outLength >= kSetupWifiPayloadMaxBytes) return false;
    output[outLength++] = value;
    output[outLength] = '\0';
    return true;
  };

  for (std::size_t i = 0; i < prefixLength; ++i) {
    if (!append(kPrefix[i])) {
      output[0] = '\0';
      return false;
    }
  }
  for (std::size_t i = 0; i < apLength; ++i) {
    const unsigned char byte = static_cast<unsigned char>(ap[i]);
    if (detail::wifiQrControl(byte)) {
      output[0] = '\0';
      return false;
    }
    if (detail::wifiQrReserved(static_cast<char>(byte)) && !append('\\')) {
      output[0] = '\0';
      return false;
    }
    if (!append(static_cast<char>(byte))) {
      output[0] = '\0';
      return false;
    }
  }
  for (std::size_t i = 0; i < suffixLength; ++i) {
    if (!append(kSuffix[i])) {
      output[0] = '\0';
      return false;
    }
  }
  return true;
}

}  // namespace round_ui
