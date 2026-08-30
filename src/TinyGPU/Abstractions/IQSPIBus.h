#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief Abstraction over the transport used by DisplayDriverQSPI (see
 * TinyGPU/Drivers/DisplayDriverQSPI.h) to talk to the family of QSPI TFT
 * panel controllers (NV3041A, ST77916, CO5300, SH8601, AXS15231B, ...)
 * that wrap every command in a 32-bit frame: opcode byte (0x02 = "write,
 * parameter bytes follow over 1 line", 0x32 = "write, color/pixel data
 * follows over 4 lines"), the MIPI-DCS-style command byte, then 16 unused
 * bits.
 *
 * DisplayDriverQSPI and its panel subclasses (NV3041ADriver, ...) only
 * call writeCommand()/writeColor() - they never build the 32-bit frame or
 * touch GPIO/peripheral registers themselves, so a new platform gains
 * support for this whole panel family by implementing just this
 * interface. See QSPIBusESP32.h (hardware esp_lcd-backed, DMA, ESP32
 * only) and QSPIBusBitBang.h (portable software fallback, any Arduino
 * core with digitalWrite - RP2040, STM32, ...).
 */
class IQSPIBus {
 public:
  virtual ~IQSPIBus() = default;

  /// Sets up the bus (pins/peripheral). Returns false on failure.
  virtual bool begin() = 0;

  /// Releases any peripheral resources. Safe to call even if begin() was
  /// never called or already failed.
  virtual void end() = 0;

  /// Sends one MIPI-DCS/vendor register command with up to `len`
  /// parameter bytes, framed with the 0x02 ("param bytes over 1 line")
  /// opcode. Returns false on transport failure.
  virtual bool writeCommand(uint8_t cmd, const uint8_t* param,
                            size_t len) = 0;

  /// Sends `len` bytes of pixel/color data for the given RAM-write
  /// command (usually 0x2C), framed with the 0x32 ("data over 4 lines")
  /// opcode. Returns false on transport failure. Blocks until the whole
  /// transfer has been accepted by the panel (synchronous from the
  /// caller's point of view, even if the underlying transport is
  /// internally asynchronous/DMA-driven).
  virtual bool writeColor(uint8_t cmd, const uint8_t* data, size_t len) = 0;
};

}  // namespace tinygpu
