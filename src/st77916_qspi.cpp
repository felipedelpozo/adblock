#include "st77916_qspi.h"

#if defined(ROUND_DISPLAY_S3)

#include <Arduino.h>

#include <algorithm>
#include <cstring>

namespace round_display_s3 {
namespace {

constexpr uint8_t kQspiWriteCommand = 0x02;
constexpr uint8_t kQspiWriteColor = 0x32;
constexpr uint32_t kRamWriteAddress = 0x2C00;
constexpr uint32_t kRamWriteContinueAddress = 0x3C00;
constexpr uint8_t kRamWriteCommand = 0x2C;
constexpr uint8_t kRamWriteContinueCommand = 0x3C;

}  // namespace

bool St77916QspiBus::init() {
  if (_initialized) return true;

  spi_bus_config_t busConfig = {};
  busConfig.sclk_io_num = kSclkPin;
  busConfig.data0_io_num = kData0Pin;
  busConfig.data1_io_num = kData1Pin;
  busConfig.data2_io_num = kData2Pin;
  busConfig.data3_io_num = kData3Pin;
  busConfig.data4_io_num = -1;
  busConfig.data5_io_num = -1;
  busConfig.data6_io_num = -1;
  busConfig.data7_io_num = -1;
  busConfig.max_transfer_sz = static_cast<int>(kTransferBufferBytes);
  busConfig.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;

  esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(kHost), &busConfig,
                                      SPI_DMA_CH_AUTO);
  if (err != ESP_OK) return false;

  spi_device_interface_config_t deviceConfig = {};
  // The panel's 32-bit QSPI header is an 8-bit opcode plus a 24-bit
  // single-line address.  These defaults are used by every transaction;
  // the per-transaction payload flag below selects QIO only for pixels.
  deviceConfig.command_bits = 8;
  deviceConfig.address_bits = 24;
  deviceConfig.clock_speed_hz = static_cast<int>(kWriteFrequencyHz);
  deviceConfig.mode = 0;
  deviceConfig.spics_io_num = kCsPin;
  deviceConfig.queue_size = 1;
  deviceConfig.flags = SPI_DEVICE_HALFDUPLEX;

  err = spi_bus_add_device(static_cast<spi_host_device_t>(kHost), &deviceConfig, &_device);
  if (err != ESP_OK) {
    spi_bus_free(static_cast<spi_host_device_t>(kHost));
    _device = nullptr;
    return false;
  }

  _initialized = true;
  _pendingCommand = -1;
  _pixelStreamActive = false;
  _pixelStreamStarted = false;
  return true;
}

void St77916QspiBus::release() {
  if (_transaction) endTransaction();
  if (_device != nullptr) {
    spi_bus_remove_device(_device);
    _device = nullptr;
  }
  if (_initialized) {
    spi_bus_free(static_cast<spi_host_device_t>(kHost));
    _initialized = false;
  }
  _pendingCommand = -1;
  _pixelStreamActive = false;
  _pixelStreamStarted = false;
}

void St77916QspiBus::beginTransaction() {
  if (_transaction || !_initialized || _device == nullptr) return;
  if (spi_device_acquire_bus(_device, portMAX_DELAY) == ESP_OK) {
    _transaction = true;
  }
}

void St77916QspiBus::endTransaction() {
  if (!_transaction) return;
  sendPendingCommand();
  spi_device_release_bus(_device);
  _transaction = false;
  _pixelStreamActive = false;
  _pixelStreamStarted = false;
}

bool St77916QspiBus::transmit(uint8_t opcode, uint32_t address, const uint8_t* data,
                              size_t length) {
  if (!_initialized || _device == nullptr || length > kTransferBufferBytes) return false;

  spi_transaction_ext_t transaction = {};
  // The ST77916 QSPI header is sent as an 8-bit single-line opcode followed
  // by a 24-bit single-line address.  Only the payload of a RAM write uses
  // all four data lines; command parameters stay on the single data line.
  const bool isPixelPayload = opcode == kQspiWriteColor;
  transaction.base.flags = isPixelPayload ? SPI_TRANS_MODE_QIO : 0;
  transaction.base.cmd = opcode;
  transaction.base.addr = address & 0x00FFFFFFU;
  transaction.base.length = length * 8U;
  transaction.base.tx_buffer = data;
  return spi_device_polling_transmit(_device, &transaction.base) == ESP_OK;
}

bool St77916QspiBus::transmitCommand(uint8_t command, const uint8_t* data, size_t length) {
  if (length > kTransferBufferBytes) return false;
  if (length != 0 && data != _transferBuffer) {
    std::memcpy(_transferBuffer, data, length);
  }
  return transmit(kQspiWriteCommand, static_cast<uint32_t>(command) << 8,
                  length == 0 ? nullptr : _transferBuffer, length);
}

bool St77916QspiBus::transmitPixels(const uint8_t* data, size_t length) {
  if (length == 0 || length > kTransferBufferBytes) return length == 0;
  if (data != _transferBuffer) std::memcpy(_transferBuffer, data, length);

  const uint32_t address = _pixelStreamStarted ? kRamWriteContinueAddress : kRamWriteAddress;
  const bool sent = transmit(kQspiWriteColor, address, _transferBuffer, length);
  if (sent) _pixelStreamStarted = true;
  return sent;
}

bool St77916QspiBus::sendPendingCommand() {
  if (_pendingCommand < 0) return true;
  const uint8_t command = static_cast<uint8_t>(_pendingCommand);
  _pendingCommand = -1;
  return transmitCommand(command, nullptr, 0);
}

bool St77916QspiBus::writeCommandData(uint8_t command, const uint8_t* data, size_t length) {
  if (!sendPendingCommand() || length > kTransferBufferBytes) return false;
  return transmitCommand(command, data, length);
}

bool St77916QspiBus::writeCommand(uint32_t data, uint_fast8_t bitLength) {
  if (bitLength == 0) return false;
  if (!sendPendingCommand()) return false;

  const uint_fast8_t bytes = static_cast<uint_fast8_t>((bitLength + 7U) >> 3U);
  const uint8_t command = (bytes <= 1U)
                              ? static_cast<uint8_t>(data)
                              : static_cast<uint8_t>(data >> ((bytes - 1U) * 8U));

  if (command == kRamWriteCommand || command == kRamWriteContinueCommand) {
    _pendingCommand = -1;
    _pixelStreamActive = true;
    _pixelStreamStarted = command == kRamWriteContinueCommand;
    return true;
  }

  _pixelStreamActive = false;
  _pixelStreamStarted = false;
  _pendingCommand = command;
  return true;
}

bool St77916QspiBus::sendPixelValue(uint32_t data, uint_fast8_t bitLength) {
  const size_t bytes = (bitLength + 7U) >> 3U;
  if (bytes == 0 || bytes > kTransferBufferBytes) return false;
  for (size_t i = 0; i < bytes; ++i) {
    _transferBuffer[i] = static_cast<uint8_t>(data >> (i * 8U));
  }
  return transmitPixels(_transferBuffer, bytes);
}

bool St77916QspiBus::sendRepeatedPixels(uint32_t data, uint_fast8_t bitLength,
                                        uint32_t count) {
  const size_t bytes = (bitLength + 7U) >> 3U;
  if (bytes == 0 || bytes > kTransferBufferBytes) return false;

  while (count != 0) {
    const uint32_t chunkPixels = std::min<uint32_t>(
        count, static_cast<uint32_t>(kTransferBufferBytes / bytes));
    const size_t chunkBytes = static_cast<size_t>(chunkPixels) * bytes;
    for (uint32_t pixel = 0; pixel < chunkPixels; ++pixel) {
      for (size_t byte = 0; byte < bytes; ++byte) {
        _transferBuffer[static_cast<size_t>(pixel) * bytes + byte] =
            static_cast<uint8_t>(data >> (byte * 8U));
      }
    }
    if (!transmitPixels(_transferBuffer, chunkBytes)) return false;
    count -= chunkPixels;
  }
  return true;
}

void St77916QspiBus::writeData(uint32_t data, uint_fast8_t bitLength) {
  if (bitLength == 0) return;

  if (_pendingCommand >= 0) {
    const size_t bytes = (bitLength + 7U) >> 3U;
    if (bytes > kTransferBufferBytes) {
      _pendingCommand = -1;
      return;
    }
    for (size_t i = 0; i < bytes; ++i) {
      _transferBuffer[i] = static_cast<uint8_t>(data >> (i * 8U));
    }
    const uint8_t command = static_cast<uint8_t>(_pendingCommand);
    _pendingCommand = -1;
    transmitCommand(command, _transferBuffer, bytes);
    return;
  }

  if (_pixelStreamActive) sendPixelValue(data, bitLength);
}

void St77916QspiBus::writeDataRepeat(uint32_t data, uint_fast8_t bitLength, uint32_t count) {
  if (count == 0 || bitLength == 0) return;

  if (_pendingCommand >= 0) {
    const size_t bytes = (bitLength + 7U) >> 3U;
    const uint64_t totalBytes = static_cast<uint64_t>(bytes) * count;
    if (bytes == 0 || totalBytes > kTransferBufferBytes) {
      _pendingCommand = -1;
      return;
    }
    for (uint32_t pixel = 0; pixel < count; ++pixel) {
      for (size_t byte = 0; byte < bytes; ++byte) {
        _transferBuffer[static_cast<size_t>(pixel) * bytes + byte] =
            static_cast<uint8_t>(data >> (byte * 8U));
      }
    }
    const uint8_t command = static_cast<uint8_t>(_pendingCommand);
    _pendingCommand = -1;
    transmitCommand(command, _transferBuffer, static_cast<size_t>(totalBytes));
    return;
  }

  if (_pixelStreamActive) sendRepeatedPixels(data, bitLength, count);
}

void St77916QspiBus::writePixels(lgfx::pixelcopy_t* param, uint32_t length) {
  if (param == nullptr || param->fp_copy == nullptr || length == 0) return;

  const size_t bytes = param->dst_bits >> 3U;
  if (bytes == 0 || bytes > kTransferBufferBytes) return;

  _pixelStreamActive = true;
  while (length != 0) {
    const uint32_t chunkPixels = std::min<uint32_t>(
        length, static_cast<uint32_t>(kTransferBufferBytes / bytes));
    const size_t chunkBytes = static_cast<size_t>(chunkPixels) * bytes;
    param->fp_copy(_transferBuffer, 0, chunkPixels, param);
    if (!transmitPixels(_transferBuffer, chunkBytes)) return;
    length -= chunkPixels;
  }
}

void St77916QspiBus::writeBytes(const uint8_t* data, uint32_t length, bool dc, bool useDma) {
  (void)useDma;
  if (data == nullptr || length == 0) return;

  if (!dc) {
    if (_pendingCommand < 0) return;
    const size_t chunk = std::min<size_t>(length, kTransferBufferBytes);
    const uint8_t command = static_cast<uint8_t>(_pendingCommand);
    _pendingCommand = -1;
    transmitCommand(command, data, chunk);
    return;
  }

  if (_pendingCommand >= 0) sendPendingCommand();
  _pixelStreamActive = true;
  while (length != 0) {
    const size_t chunk = std::min<size_t>(length, kTransferBufferBytes);
    if (data != _transferBuffer) std::memcpy(_transferBuffer, data, chunk);
    if (!transmitPixels(_transferBuffer, chunk)) return;
    data += chunk;
    length -= static_cast<uint32_t>(chunk);
  }
}

void St77916QspiBus::flush() {
  // Parameter writes consume the pending command immediately.  A command
  // without parameters is emitted at the transaction boundary, allowing
  // command_list-style callers to issue parameters after their flush call.
}

void St77916QspiBus::addDMAQueue(const uint8_t* data, uint32_t length) {
  writeBytes(data, length, true, true);
}

uint8_t* St77916QspiBus::getDMABuffer(uint32_t length) {
  return length <= kTransferBufferBytes ? _transferBuffer : nullptr;
}

bool St77916QspiBus::readBytes(uint8_t* dst, uint32_t length, bool useDma) {
  (void)useDma;
  if (dst != nullptr && length != 0) std::memset(dst, 0, length);
  return false;
}

bool St77916QspiBus::readBytes(uint8_t* dst, uint32_t length, bool useDma, bool lastNack) {
  (void)lastNack;
  return readBytes(dst, length, useDma);
}

void St77916QspiBus::readPixels(void* dst, lgfx::pixelcopy_t* param, uint32_t length) {
  if (dst == nullptr || param == nullptr) return;
  const size_t bytes = param->dst_bits >> 3U;
  std::memset(dst, 0, static_cast<size_t>(length) * bytes);
}

PanelSt77916Qspi::PanelSt77916Qspi() {
  _cfg.pin_cs = -1;  // SPI2 owns CS10; no software CS toggling is needed.
  _cfg.pin_rst = 47;
  _cfg.pin_busy = -1;
  _cfg.memory_width = 360;
  _cfg.memory_height = 360;
  _cfg.panel_width = 360;
  _cfg.panel_height = 360;
  _cfg.offset_x = 0;
  _cfg.offset_y = 0;
  _cfg.offset_rotation = 0;
  _cfg.readable = false;
  _cfg.bus_shared = false;
  _cfg.rgb_order = false;  // the JC3636W518C vendor sequence is BGR.
  _cfg.dlen_16bit = false;
  _cfg.invert = false;
  _invert = true;  // manufacturer demo calls invertColor(true).
  _nop_closing = false;
}

St77916QspiBus* PanelSt77916Qspi::qspiBus() {
  return static_cast<St77916QspiBus*>(getBus());
}

bool PanelSt77916Qspi::init(bool useReset) {
  auto* bus = qspiBus();
  if (bus == nullptr) return false;
  if (!lgfx::Panel_Device::init(useReset) || !bus->isInitialized()) return false;

  // Panel_Device waits 64 ms after releasing reset.  The manufacturer
  // sequence expects the full 120 ms reset recovery interval.
  if (useReset) delay(56);

  startWrite();
  bool ok = true;
  const uint8_t madctl = 0x08;  // BGR, rotation 0
  const uint8_t colmod = 0x55;  // RGB565
  ok = bus->writeCommandData(0x36, &madctl, 1) && ok;
  ok = bus->writeCommandData(0x3A, &colmod, 1) && ok;

  for (size_t i = 0; ok && i < kSt77916InitCommandCount; ++i) {
    const auto& init = kSt77916InitCommands[i];
    ok = bus->writeCommandData(init.command, init.data, init.dataLength);
    if (ok && init.delayMs != 0) delay(init.delayMs);
  }
  endWrite();
  return ok;
}

}  // namespace round_display_s3

RoundDisplayS3::RoundDisplayS3() {
  panel_.setBus(&bus_);
  setPanel(&panel_);
}

#endif  // defined(ROUND_DISPLAY_S3)
