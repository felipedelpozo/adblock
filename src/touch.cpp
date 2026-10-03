#include "touch.h"

#if defined(ROUND_DISPLAY)

#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t kChipIdRegister = 0xA7;
constexpr uint8_t kTouchDataRegister = 0x02;
constexpr uint32_t kResetLowMs = 5;
constexpr uint32_t kProbeWaitMs = 100;
constexpr uint32_t kProbeRetryMs = 250;
constexpr uint32_t kTouchPollMs = 20;

bool resetHeld = false;
bool chipProbed = false;
bool chipSupported = false;
bool fingerDown = false;
uint8_t chipId = 0;
uint32_t resetReleaseAt = 0;
uint32_t probeAt = 0;
uint32_t touchPollAt = 0;

bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

bool readRegisters(uint8_t reg, uint8_t* data, uint8_t length) {
  Wire.beginTransmission(round_ui::pins::kTouchAddress);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  const uint8_t received = Wire.requestFrom(static_cast<int>(round_ui::pins::kTouchAddress),
                                             static_cast<int>(length), static_cast<int>(true));
  if (received != length) return false;
  for (uint8_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

bool supported(uint8_t id) { return id == 0xB4 || id == 0xB5 || id == 0xB6; }

}  // namespace

namespace round_ui::touch {

bool begin() {
  pinMode(round_ui::pins::kTouchReset, OUTPUT);
  digitalWrite(round_ui::pins::kTouchReset, LOW);
  pinMode(round_ui::pins::kTouchInterrupt, INPUT_PULLUP);
  Wire.begin(round_ui::pins::kTouchSda, round_ui::pins::kTouchScl);
  Wire.setClock(400000);
  // A failed CST device must never hold the application in an I2C wait.
  Wire.setTimeOut(20);
  resetHeld = true;
  chipProbed = false;
  chipSupported = false;
  fingerDown = false;
  chipId = 0;
  resetReleaseAt = millis() + kResetLowMs;
  probeAt = resetReleaseAt + kProbeWaitMs;
  touchPollAt = 0;
  return true;
}

bool ready() { return chipProbed && chipSupported; }

Point poll(uint32_t now) {
  Point point;
  if (resetHeld) {
    if (!due(now, resetReleaseAt)) return point;
    digitalWrite(round_ui::pins::kTouchReset, HIGH);
    resetHeld = false;
    // Wi-Fi scanning can postpone the first poll. Measure recovery from the
    // actual reset release, not the originally scheduled release deadline.
    probeAt = now + kProbeWaitMs;
    return point;
  }

  if (!chipProbed) {
    if (!due(now, probeAt)) return point;
    uint8_t value = 0;
    if (!readRegisters(kChipIdRegister, &value, 1)) {
      probeAt = now + kProbeRetryMs;
      return point;
    }
    chipId = value;
    chipSupported = supported(chipId);
    chipProbed = true;
    Serial.printf("[round-touch] chip=0x%02X supported=%s\n", chipId, chipSupported ? "yes" : "no");
    return point;
  }
  if (!chipSupported) return point;

  // Poll at most every 20 ms, including release. CST revisions that pulse INT
  // do not retain the interrupt level until the application can read it.
  if (!due(now, touchPollAt)) return point;
  touchPollAt = now + kTouchPollMs;
  uint8_t data[5] = {};
  if (!readRegisters(kTouchDataRegister, data, sizeof(data))) return point;
  const uint8_t touches = data[0] & 0x0fU;
  if (touches == 0) {
    fingerDown = false;
    return point;
  }
  if (fingerDown) return point;
  fingerDown = true;
  point.x = static_cast<int16_t>(((data[1] & 0x0fU) << 8) | data[2]);
  point.y = static_cast<int16_t>(((data[3] & 0x0fU) << 8) | data[4]);
  // Both profiles use the same logical 240x240 controls and rotation zero.
  if (point.x >= pins::kPanelSize || point.y >= pins::kPanelSize) return {};
  point.x = (point.x * 240) / pins::kPanelSize;
  point.y = (point.y * 240) / pins::kPanelSize;
  point.valid = true;
  return point;
}

}  // namespace round_ui::touch

#else

namespace round_ui::touch {
bool begin() { return false; }
bool ready() { return false; }
Point poll(uint32_t) { return {}; }
}  // namespace round_ui::touch

#endif
