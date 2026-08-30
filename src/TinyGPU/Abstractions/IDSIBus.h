#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief Abstraction over the transport used by DisplayDriverDSI (see
 * TinyGPU/Drivers/DisplayDriverDSI.h) to talk to MIPI-DSI panels: a
 * low-speed command channel (DCS/vendor register writes, used for the
 * panel's init sequence) plus a continuously-scanned video channel
 * backed by a framebuffer that draws address into directly - there's no
 * address-window/RAMWR concept the way SPI/QSPI panels have.
 *
 * DisplayDriverDSI and its panel subclasses (ST7701Driver, ...) only call
 * writeCommand()/drawBitmap() - they never touch the DSI PHY, lane
 * config, or video-timing/LTDC registers directly, so a new platform
 * gains support for MIPI-DSI panels by implementing just this interface.
 * See DSIBusESP32.h (ESP32-P4's esp_lcd DBI+DPI split) and
 * DSIBusSTM32.h (STM32's HAL_DSI + LTDC, on the STM32 parts that have a
 * DSI host: F469/F479, F769/F779, H747/H757, U5x9, ...). RP2040 has no
 * MIPI-DSI peripheral on any variant, so there is no RP2040 backend for
 * this interface - QSPI/SPI panels (see IQSPIBus.h/DisplayDriverSPI.h)
 * are the only display option on that platform.
 */
class IDSIBus {
 public:
  virtual ~IDSIBus() = default;

  /// Powers up and configures the DSI PHY/lanes and the command channel,
  /// and pulses panel reset if the concrete bus owns that pin. After this
  /// returns true, writeCommand() can be used to send the panel's init
  /// register sequence. Does not yet start active video scan-out - see
  /// finishInit().
  virtual bool begin() = 0;

  /// Releases peripheral/PHY resources. Safe to call even if begin() was
  /// never called or already failed.
  virtual void end() = 0;

  /// Sends one DCS/vendor register command with up to `len` parameter
  /// bytes over the DSI command channel. Returns false on transport
  /// failure.
  virtual bool writeCommand(uint8_t cmd, const uint8_t* param,
                            size_t len) = 0;

  /// Finalizes bring-up after the panel's init sequence has been sent -
  /// configures/starts the video-timing channel so the panel begins
  /// active scan-out. Must be called exactly once, after begin() and
  /// before the first drawBitmap().
  virtual bool finishInit() = 0;

  /// Writes `rgb565` (native RGB565 byte order, width*height*2 bytes,
  /// tightly packed) into the rectangle [x0,y0)-[x1,y1) of the live
  /// framebuffer. Returns false on transport failure.
  virtual bool drawBitmap(size_t x0, size_t y0, size_t x1, size_t y1,
                          const uint8_t* rgb565) = 0;
};

}  // namespace tinygpu
