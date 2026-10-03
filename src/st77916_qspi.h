#pragma once

// The JC3636W518C panel is only present on the ESP32-S3 target.  Keeping the
// complete driver behind this guard lets the C3 and headless environments keep
// compiling without pulling in the ESP-IDF SPI master API.
#if defined(ROUND_DISPLAY_S3)

#include <LovyanGFX.hpp>
#include <driver/spi_master.h>
#include <lgfx/v1/Bus.hpp>
#include <lgfx/v1/panel/Panel_LCD.hpp>

#include <cstddef>
#include <cstdint>

#include "st77916_init.h"

namespace round_display_s3 {

class St77916QspiBus final : public lgfx::IBus {
 public:
  static constexpr int kHost = SPI2_HOST;
  static constexpr int kSclkPin = 9;
  static constexpr int kCsPin = 10;
  static constexpr int kData0Pin = 11;
  static constexpr int kData1Pin = 12;
  static constexpr int kData2Pin = 13;
  static constexpr int kData3Pin = 14;
  static constexpr uint32_t kWriteFrequencyHz = 40U * 1000U * 1000U;
  static constexpr size_t kTransferBufferBytes = 1024;

  St77916QspiBus() = default;
  ~St77916QspiBus() override { release(); }

  lgfx::bus_type_t busType() const override { return lgfx::bus_type_t::bus_spi; }
  bool init() override;
  void release() override;
  uint32_t getClock() const override { return kWriteFrequencyHz; }
  uint32_t getReadClock() const override { return 0; }
  void setClock(uint32_t) override {}
  void setReadClock(uint32_t) override {}

  void beginTransaction() override;
  void endTransaction() override;
  void wait() override {}
  bool busy() const override { return false; }

  void initDMA() override {}
  void addDMAQueue(const uint8_t* data, uint32_t length) override;
  void execDMAQueue() override {}
  uint8_t* getDMABuffer(uint32_t length) override;

  void flush() override;
  bool writeCommand(uint32_t data, uint_fast8_t bitLength) override;
  void writeData(uint32_t data, uint_fast8_t bitLength) override;
  void writeDataRepeat(uint32_t data, uint_fast8_t bitLength, uint32_t count) override;
  void writePixels(lgfx::pixelcopy_t* param, uint32_t length) override;
  void writeBytes(const uint8_t* data, uint32_t length, bool dc, bool useDma) override;

  void beginRead() override {}
  void endRead() override {}
  uint32_t readData(uint_fast8_t) override { return 0; }
  bool readBytes(uint8_t* dst, uint32_t length, bool useDma = false) override;
  bool readBytes(uint8_t* dst, uint32_t length, bool useDma, bool lastNack) override;
  void readPixels(void* dst, lgfx::pixelcopy_t* param, uint32_t length) override;

  // Used by the panel's vendor initialization path.  Keeping this operation
  // here ensures each ST77916 command and all of its parameters share one
  // QSPI transaction, as required by the panel protocol.
  bool writeCommandData(uint8_t command, const uint8_t* data, size_t length);
  bool isInitialized() const { return _initialized; }

 private:
  bool transmit(uint8_t opcode, uint32_t address, const uint8_t* data, size_t length);
  bool transmitCommand(uint8_t command, const uint8_t* data, size_t length);
  bool transmitPixels(const uint8_t* data, size_t length);
  bool sendPendingCommand();
  bool sendPixelValue(uint32_t data, uint_fast8_t bitLength);
  bool sendRepeatedPixels(uint32_t data, uint_fast8_t bitLength, uint32_t count);

  spi_device_handle_t _device = nullptr;
  bool _initialized = false;
  bool _transaction = false;
  int16_t _pendingCommand = -1;
  bool _pixelStreamActive = false;
  bool _pixelStreamStarted = false;
  alignas(4) uint8_t _transferBuffer[kTransferBufferBytes] = {};
};

class PanelSt77916Qspi final : public lgfx::Panel_LCD {
 public:
  PanelSt77916Qspi();
  bool init(bool useReset) override;

 private:
  St77916QspiBus* qspiBus();
};

}  // namespace round_display_s3

// This is intentionally global: the application uses it exactly like the
// other LovyanGFX device classes in the repository.
class RoundDisplayS3 final : public lgfx::LGFX_Device {
 public:
  RoundDisplayS3();

 private:
  round_display_s3::St77916QspiBus bus_;
  round_display_s3::PanelSt77916Qspi panel_;
};

#endif  // defined(ROUND_DISPLAY_S3)
