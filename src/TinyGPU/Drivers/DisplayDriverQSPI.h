#pragma once
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Abstractions/IQSPIBus.h"
#include "TinyGPU/Abstractions/QSPIBusBitBang.h"
#include "TinyGPU/Emulation.h"

#if defined(ESP32)
#include "TinyGPU/Abstractions/QSPIBusESP32.h"
#endif

namespace tinygpu {

/**
 * @brief Base driver for QSPI ("4-wire quad SPI") TFT panels: 1 clock, 1
 * chip-select, 4 data lines, no separate D/C pin. Used by a family of
 * compact panel controllers (NV3041A, ST77916, CO5300, SH8601,
 * AXS15231B, ...) that all speak a de-facto-standardized QSPI wire
 * protocol - see IQSPIBus.h for the exact 32-bit command-frame/data
 * layout.
 *
 * The wire protocol is handled by an IQSPIBus (dependency-injected, see
 * the two constructors below), not by this class - this class only turns
 * ISurface writes into writeColor() calls and exposes writeCommand() to
 * subclasses for their chip-specific init sequence, the same division of
 * labor DisplayDriverSPI's setupPinsAndReset() uses for its SPI-panel
 * subclasses. That split is what makes this family portable: adding a
 * platform means implementing IQSPIBus once (see QSPIBusESP32.h,
 * hardware/DMA-backed; QSPIBusBitBang.h, portable software fallback for
 * RP2040/STM32/anything else), while every panel subclass (NV3041ADriver,
 * ...) keeps working unchanged on every platform that has a bus.
 */
template <typename RGB_T = RGB565>
class DisplayDriverQSPI : public DisplayDriver<RGB_T> {
 public:
  /// Pin-based constructor: builds and owns the platform's default
  /// IQSPIBus (QSPIBusESP32 on ESP32, QSPIBusBitBang everywhere else).
  /// pclkHz only affects the ESP32 backend - QSPIBusBitBang runs as fast
  /// as digitalWrite() allows, uncapped.
  DisplayDriverQSPI(int8_t cs, int8_t sclk, int8_t d0, int8_t d1, int8_t d2,
                    int8_t d3, size_t width, size_t height,
                    uint32_t pclkHz = 40000000
#if defined(ESP32)
                    ,
                    spi_host_device_t host = SPI3_HOST
#endif
                    )
      : width_(width), height_(height) {
#if defined(ESP32)
    ownedBus_ = new QSPIBusESP32(cs, sclk, d0, d1, d2, d3,
                                 width * height * sizeof(RGB_T), pclkHz, host);
#else
    (void)pclkHz;
    ownedBus_ = new QSPIBusBitBang(cs, sclk, d0, d1, d2, d3);
#endif
    bus_ = ownedBus_;
  }

  /// Bus-injection constructor: use any IQSPIBus implementation (e.g. a
  /// future PIO-accelerated RP2040 bus) without changing this class or
  /// its panel subclasses. The caller keeps ownership of `bus`.
  DisplayDriverQSPI(IQSPIBus& bus, size_t width, size_t height)
      : bus_(&bus), width_(width), height_(height) {}

  void end() override {
    if (bus_ != nullptr) bus_->end();
  }

  ~DisplayDriverQSPI() override {
    delete ownedBus_;
  }

  size_t width() const override { return width_; }
  size_t height() const override { return height_; }

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    static_assert(sizeof(RGB_T) == 2,
                  "writeData assumes a 16bpp RGB_T (RGB565) stored in "
                  "wire byte order");
    if (bus_ == nullptr) return false;
    if (!setAddressWindow(x, y, surface.width(), surface.height())) {
      return false;
    }

    // RGB_T (RGB565) values are stored in the surface already
    // byte-swapped to this panel's wire byte order (see RGB565.h), so no
    // per-pixel swap is needed here - the surface's raw memory can go
    // straight to the bus.
    return bus_->writeColor(kCmdRamWrite, surface.data(), surface.size());
  }

 protected:
  static constexpr uint8_t kCmdRamWrite = 0x2C;

  /// Sets up the bus. Subclasses call this first thing in their begin(),
  /// then send their chip's init register sequence via writeCommand()
  /// before returning.
  bool beginBus() { return bus_ != nullptr && bus_->begin(); }

  bool writeCommand(uint8_t cmd, const uint8_t* param = nullptr,
                    size_t len = 0) {
    return bus_ != nullptr && bus_->writeCommand(cmd, param, len);
  }

  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    const uint8_t caset[4] = {
        static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
        static_cast<uint8_t>((x + w - 1) >> 8),
        static_cast<uint8_t>((x + w - 1) & 0xFF)};
    const uint8_t raset[4] = {
        static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF),
        static_cast<uint8_t>((y + h - 1) >> 8),
        static_cast<uint8_t>((y + h - 1) & 0xFF)};
    return writeCommand(0x2A, caset, 4) && writeCommand(0x2B, raset, 4);
  }

  IQSPIBus* bus_ = nullptr;
  size_t width_, height_;

 private:
  IQSPIBus* ownedBus_ = nullptr;
};

/**
 * @brief Driver for the New Vision NV3041A QSPI TFT controller (480x272
 * panels on Guition/Sunton JC4827W543-class ESP32-S3 boards).
 *
 * The full vendor register-unlock/gate/source-timing/gamma init
 * sequence, confirmed against real hardware (ported from
 * moononournation/Arduino_GFX's Arduino_NV3041A driver - an earlier
 * minimal sleep-out/pixel-format/MADCTL/display-on sequence produced a
 * rotated color channel mapping: red rendered as blue, green as red,
 * blue as green). Register 0x3A (pixel format) uses a chip-specific
 * encoding on this part, not the standard MIPI-DCS one: 0x01 selects
 * 16bpp RGB565 here, not the commonly-documented 0x55.
 *
 * Platform-independent: works over any IQSPIBus, so the same class
 * covers ESP32 (QSPIBusESP32, hardware/DMA), RP2040 and STM32
 * (QSPIBusBitBang, software) - see DisplayDriverQSPI's class comment.
 * Only bring-up on ESP32 has been confirmed against real hardware; the
 * RP2040/STM32 bit-bang path follows the same documented wire protocol
 * but has not been run on real hardware.
 */
template <typename RGB_T = RGB565>
class NV3041ADriver : public DisplayDriverQSPI<RGB_T> {
 public:
  using DisplayDriverQSPI<RGB_T>::beginBus;
  using DisplayDriverQSPI<RGB_T>::writeCommand;

  // 32MHz matches Arduino_GFX's Arduino_NV3041A driver, which documents
  // it as this chip's maximum supported speed and runs clean at it on
  // real hardware; this driver's original 40MHz default showed scattered
  // pixel corruption on large transfers, consistent with the panel/
  // wiring not reliably sustaining a faster clock. Only applies to the
  // ESP32 hardware backend - QSPIBusBitBang ignores pclkHz.
  NV3041ADriver(int8_t cs, int8_t sclk, int8_t d0, int8_t d1, int8_t d2,
               int8_t d3, size_t width = 480, size_t height = 272,
               uint32_t pclkHz = 32000000)
      : DisplayDriverQSPI<RGB_T>(cs, sclk, d0, d1, d2, d3, width, height,
                                 pclkHz) {}

  /// Bus-injection constructor - see DisplayDriverQSPI's equivalent.
  NV3041ADriver(IQSPIBus& bus, size_t width = 480, size_t height = 272)
      : DisplayDriverQSPI<RGB_T>(bus, width, height) {}

  bool begin() override {
    static_assert(sizeof(RGB_T) == 2,
                  "NV3041ADriver assumes a 16bpp RGB_T (RGB565)");

    if (!beginBus()) return false;

    for (size_t i = 0; i < sizeof(kInitOps) / sizeof(kInitOps[0]); ++i) {
      const uint8_t cmd = kInitOps[i][0];
      const uint8_t data = kInitOps[i][1];
      writeCommand(cmd, &data, 1);
      if (cmd == 0x11) delay(120);  // Sleep out
    }
    delay(100);  // after display on (0x29, the last op in kInitOps)

    // This panel is an IPS type: its default (uninverted) state and the
    // vendor gamma table above only produce correct colors with display
    // inversion turned ON - without this, colors come out wrong in a
    // gamma-dependent, non-uniform way (not a clean photographic
    // negative), which is why this was easy to miss.
    writeCommand(0x21);  // INVON

    return true;
  }

 private:
  // {command, data} pairs, in order. Ported verbatim from
  // moononournation/Arduino_GFX's nv3041a_init_operations - register
  // meanings beyond 0x3A/0x36/0x11/0x29 (gate/source timing, gamma,
  // power) are vendor-specific and undocumented beyond that source.
  static constexpr uint8_t kInitOps[][2] = {
      {0xff, 0xa5},  // register unlock
      {0x36, 0xc0},  // MADCTL
      {0x3A, 0x01},  // pixel format: 01=565, 00=666 (chip-specific encoding)
      {0x41, 0x03},  // 01=8bit, 03=16bit
      {0x44, 0x15}, {0x45, 0x15},  // VBP/VFP
      {0x7d, 0x03},
      {0xc1, 0xbb}, {0xc2, 0x05}, {0xc3, 0x10},
      {0xc6, 0x3e}, {0xc7, 0x25}, {0xc8, 0x11},
      {0x7a, 0x5f}, {0x6f, 0x44}, {0x78, 0x70},
      {0xc9, 0x00}, {0x67, 0x21},
      {0x51, 0x0a}, {0x52, 0x76}, {0x53, 0x0a}, {0x54, 0x76},  // gate timing
      {0x46, 0x0a}, {0x47, 0x2a}, {0x48, 0x0a}, {0x49, 0x1a},  // source timing
      {0x56, 0x43}, {0x57, 0x42}, {0x58, 0x3c}, {0x59, 0x64},
      {0x5a, 0x41}, {0x5b, 0x3c}, {0x5c, 0x02}, {0x5d, 0x3c},
      {0x5e, 0x1f}, {0x60, 0x80}, {0x61, 0x3f}, {0x62, 0x21},
      {0x63, 0x07}, {0x64, 0xe0}, {0x65, 0x02},
      {0xca, 0x20}, {0xcb, 0x52}, {0xcc, 0x10}, {0xcD, 0x42},
      {0xD0, 0x20}, {0xD1, 0x52}, {0xD2, 0x10}, {0xD3, 0x42},
      {0xD4, 0x0a}, {0xD5, 0x32},
      // gamma
      {0x80, 0x00}, {0xA0, 0x00}, {0x81, 0x07}, {0xA1, 0x06},
      {0x82, 0x02}, {0xA2, 0x01}, {0x86, 0x11}, {0xA6, 0x10},
      {0x87, 0x27}, {0xA7, 0x27}, {0x83, 0x37}, {0xA3, 0x37},
      {0x84, 0x35}, {0xA4, 0x35}, {0x85, 0x3f}, {0xA5, 0x3f},
      {0x88, 0x0b}, {0xA8, 0x0b}, {0x89, 0x14}, {0xA9, 0x14},
      {0x8a, 0x1a}, {0xAa, 0x1a}, {0x8b, 0x0a}, {0xAb, 0x0a},
      {0x8c, 0x14}, {0xAc, 0x08}, {0x8d, 0x17}, {0xAd, 0x07},
      {0x8e, 0x16}, {0xAe, 0x06}, {0x8f, 0x1B}, {0xAf, 0x07},
      {0x90, 0x04}, {0xB0, 0x04}, {0x91, 0x0A}, {0xB1, 0x0A},
      {0x92, 0x16}, {0xB2, 0x15},
      {0xff, 0x00},  // close register bank
      {0x11, 0x00},  // sleep out (triggers the 120ms delay above)
      {0x29, 0x00},  // display on
  };
};

}  // namespace tinygpu
