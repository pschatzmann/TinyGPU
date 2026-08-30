#pragma once
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Color/RGB565.h"
#include "TinyGPU/Emulation.h"
#include "TinyGPU/Emulation/IDSIBus.h"
#include "TinyGPU/Emulation/DSIBusESP32.h"
#include "TinyGPU/Emulation/DSIBusSTM32.h"

namespace tinygpu {

/**
 * @brief Base driver for MIPI-DSI panels. All of the DSI PHY/lane bring-up,
 * command-channel writes and video-timing/framebuffer scan-out are handled
 * by an IDSIBus (dependency-injected, see the constructor below), not by
 * this class - this class only turns ISurface writes into drawBitmap()
 * calls and byte-swaps into the bus's expected native RGB565 order (see
 * writeData()). That split is what makes this family portable: adding a
 * platform means implementing IDSIBus once (see DSIBusESP32.h, the
 * ESP32-P4 DBI+DPI path; DSIBusSTM32.h, STM32's HAL_DSI+LTDC path), while
 * every panel subclass (ST7701Driver, ...) keeps working unchanged on
 * every platform that has a bus.
 *
 * RP2040 has no MIPI-DSI peripheral on any variant, so there is no
 * RP2040 IDSIBus backend - QSPI/SPI panels (DisplayDriverQSPI.h/
 * DisplayDriverSPI.h) are the only display option on that platform.
 */
template <typename RGB_T = RGB565>
class DisplayDriverDSI : public DisplayDriver<RGB_T> {
 public:
  DisplayDriverDSI(IDSIBus& bus, size_t width, size_t height)
      : bus_(&bus), width_(width), height_(height) {}

  size_t width() const override { return width_; }
  size_t height() const override { return height_; }

  void end() override {
    if (bus_ != nullptr) bus_->end();
  }

  ~DisplayDriverDSI() override { delete[] scratch_; }

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    static_assert(sizeof(RGB_T) == 2,
                  "DisplayDriverDSI assumes a 16bpp RGB_T (RGB565)");
    if (bus_ == nullptr) return false;

    // RGB_T (RGB565) values are stored byte-swapped to SPI/QSPI wire order
    // (see RGB565.h) - a DSI framebuffer instead wants the conventional
    // (native) RGB565 bit layout, so swap back into a scratch buffer
    // before handing it to the bus, same as DisplayDriverSDL does and for
    // the same reason (this is a plain memory framebuffer, not a
    // command-driven SPI panel).
    const size_t n = surface.size();
    if (scratchCapacity_ < n) {
      delete[] scratch_;
      scratch_ = new uint8_t[n];
      scratchCapacity_ = n;
    }
    const uint16_t* src = reinterpret_cast<const uint16_t*>(surface.data());
    uint16_t* dst = reinterpret_cast<uint16_t*>(scratch_);
    const size_t pixelCount = n / sizeof(RGB_T);
    for (size_t i = 0; i < pixelCount; ++i) {
      dst[i] = RGB565::swapBytes(src[i]);
    }

    return bus_->drawBitmap(x, y, x + surface.width(), y + surface.height(),
                            scratch_);
  }

 protected:
  /// Sets up the bus (PHY/lanes/command channel). Subclasses call this
  /// first thing in their begin(), then send their panel's init register
  /// sequence via writeCommand(), then finishInit() - the same
  /// beginBus()-then-init-sequence-then-finishInit() division of labor
  /// DisplayDriverQSPI's subclasses use for beginBus()/writeCommand().
  bool beginBus() { return bus_ != nullptr && bus_->begin(); }

  /// Sends one command + parameter bytes over the DSI command channel.
  bool writeCommand(uint8_t cmd, const uint8_t* param = nullptr,
                    size_t len = 0) {
    return bus_ != nullptr && bus_->writeCommand(cmd, param, len);
  }

  /// Finalizes the panel after the init register sequence has been sent -
  /// active video scan-out starts once this returns true.
  bool finishInit() { return bus_ != nullptr && bus_->finishInit(); }

  void setBacklight(bool on) {
    if (backlight_ < 0) return;
    pinMode(backlight_, OUTPUT);
    digitalWrite(backlight_, on ? HIGH : LOW);
  }

  void setBacklightPin(int8_t backlight) { backlight_ = backlight; }

  bool setAddressWindow(size_t /*x*/, size_t /*y*/, size_t /*w*/,
                        size_t /*h*/) override {
    return true;  // no-op - see class comment
  }

  IDSIBus* bus_;
  size_t width_, height_;
  int8_t backlight_ = -1;

 private:
  uint8_t* scratch_ = nullptr;
  size_t scratchCapacity_ = 0;
};

#ifdef TINYGPU_HAS_ESP_LCD_DSI

/**
 * @brief Driver for the Sitronix ST7701S MIPI-DSI TFT controller, as wired
 * on the Guition JC4880P443C_I_W (ESP32-P4, 480x800 portrait, 4.3").
 *
 * Init register sequence, DSI lane config (2 lanes @ 500Mbps), DPI clock
 * (34MHz) and video timing all transcribed verbatim from
 * guition-jc4880p4-bsp's board_p4_st7701_init.h / board_p4.c
 * (https://github.com/ultramcu/guition-jc4880p4-bsp), which documents them
 * as VERIFIED-ON-HARDWARE on this exact board - this class only re-expresses
 * that same recipe as a TinyGPU DisplayDriver instead of a bespoke BSP
 * function, it does not add or change any of the underlying values. As with
 * every other example in this repo's esp32-p4-480x800-guition-mipi-dsi-touch-display
 * board directory, this file itself has not been run on real hardware.
 *
 * Register meanings beyond 0x11 (SLPOUT) / 0x29 (DISPON) are vendor-specific
 * (ST7701 command-bank-switched extension registers) and undocumented
 * beyond that source.
 */
template <typename RGB_T = RGB565>
class ST7701Driver : public DisplayDriverDSI<RGB_T> {
 public:
  using DisplayDriverDSI<RGB_T>::beginBus;
  using DisplayDriverDSI<RGB_T>::writeCommand;
  using DisplayDriverDSI<RGB_T>::finishInit;
  using DisplayDriverDSI<RGB_T>::setBacklight;
  using DisplayDriverDSI<RGB_T>::setBacklightPin;

  ST7701Driver(int8_t rst, int8_t backlight, size_t width = 480,
              size_t height = 800, int8_t phyLdoChan = 3,
              uint16_t phyLdoMv = 2500)
      : DisplayDriverDSI<RGB_T>(ownedBus_, width, height),
        ownedBus_(rst, width, height, /*laneNum=*/2, /*laneMbps=*/500,
                  /*dpiClockMhz=*/34, phyLdoChan, phyLdoMv) {
    setBacklightPin(backlight);
    ownedBus_.setVideoTiming(/*hsyncPulseWidth=*/12, /*hsyncBackPorch=*/42,
                             /*hsyncFrontPorch=*/42, /*vsyncPulseWidth=*/2,
                             /*vsyncBackPorch=*/8, /*vsyncFrontPorch=*/166);
  }

  bool begin() override {
    static_assert(sizeof(RGB_T) == 2, "ST7701Driver assumes a 16bpp RGB_T (RGB565)");

    if (!beginBus()) return false;

    for (size_t i = 0; i < sizeof(kInitOps) / sizeof(kInitOps[0]); ++i) {
      const InitOp& op = kInitOps[i];
      writeCommand(op.cmd, op.data, op.len);
      if (op.delayMs) delay(op.delayMs);
    }

    if (!finishInit()) return false;
    setBacklight(true);
    return true;
  }

 private:
  DSIBusESP32 ownedBus_;

  struct InitOp {
    uint8_t cmd;
    const uint8_t* data;
    size_t len;
    uint16_t delayMs;
  };

  // {cmd, &params, param byte count, post-delay ms}, verbatim from
  // guition-jc4880p4-bsp's board_p4_panel_init_cmds[] - see class comment.
  static constexpr uint8_t kFf13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
  static constexpr uint8_t kEf08[] = {0x08};
  static constexpr uint8_t kFf10[] = {0x77, 0x01, 0x00, 0x00, 0x10};
  static constexpr uint8_t kC0[] = {0x63, 0x00};
  static constexpr uint8_t kC1[] = {0x0D, 0x02};
  static constexpr uint8_t kC2[] = {0x10, 0x08};
  static constexpr uint8_t kCc[] = {0x10};
  static constexpr uint8_t kB0Gamma[] = {0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07,
                                         0x0C, 0x09, 0x09, 0x28, 0x06, 0xD4,
                                         0x13, 0x69, 0x2B, 0x71};
  static constexpr uint8_t kB1Gamma[] = {0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06,
                                         0x0A, 0x08, 0x08, 0x25, 0x03, 0xD3,
                                         0x12, 0x66, 0x6A, 0x0D};
  static constexpr uint8_t kFf11[] = {0x77, 0x01, 0x00, 0x00, 0x11};
  static constexpr uint8_t kB0[] = {0x5D};
  static constexpr uint8_t kB1[] = {0x58};
  static constexpr uint8_t kB2[] = {0x87};
  static constexpr uint8_t kB3[] = {0x80};
  static constexpr uint8_t kB5[] = {0x4E};
  static constexpr uint8_t kB7[] = {0x85};
  static constexpr uint8_t kB8[] = {0x21};
  static constexpr uint8_t kB9[] = {0x10, 0x1F};
  static constexpr uint8_t kBb[] = {0x03};
  static constexpr uint8_t kBc[] = {0x00};
  static constexpr uint8_t kC178[] = {0x78};
  static constexpr uint8_t kC278[] = {0x78};
  static constexpr uint8_t kD0[] = {0x88};
  static constexpr uint8_t kE0[] = {0x00, 0x3A, 0x02};
  static constexpr uint8_t kE1[] = {0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0,
                                    0x00, 0xA0, 0x00, 0x40, 0x40};
  static constexpr uint8_t kE2[] = {0x30, 0x00, 0x40, 0x40, 0x32, 0xA0,
                                    0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00};
  static constexpr uint8_t kE3[] = {0x00, 0x00, 0x33, 0x33};
  static constexpr uint8_t kE4[] = {0x44, 0x44};
  static constexpr uint8_t kE5[] = {0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30,
                                    0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0,
                                    0x07, 0x2C, 0xA0, 0xA0};
  static constexpr uint8_t kE6[] = {0x00, 0x00, 0x33, 0x33};
  static constexpr uint8_t kE7[] = {0x44, 0x44};
  static constexpr uint8_t kE8[] = {0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F,
                                    0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0,
                                    0x06, 0x2B, 0xA0, 0xA0};
  static constexpr uint8_t kEb[] = {0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00};
  static constexpr uint8_t kEc[] = {0x08, 0x01};
  static constexpr uint8_t kEd[] = {0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65,
                                    0x4A, 0x89, 0xB2, 0x0B};
  static constexpr uint8_t kEf[] = {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54};
  static constexpr uint8_t kFf00[] = {0x77, 0x01, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop[] = {0x00};

  static constexpr InitOp kInitOps[] = {
      {0xFF, kFf13, sizeof(kFf13), 0},
      {0xEF, kEf08, sizeof(kEf08), 0},
      {0xFF, kFf10, sizeof(kFf10), 0},
      {0xC0, kC0, sizeof(kC0), 0},
      {0xC1, kC1, sizeof(kC1), 0},
      {0xC2, kC2, sizeof(kC2), 0},
      {0xCC, kCc, sizeof(kCc), 0},
      {0xB0, kB0Gamma, sizeof(kB0Gamma), 0},
      {0xB1, kB1Gamma, sizeof(kB1Gamma), 0},
      {0xFF, kFf11, sizeof(kFf11), 0},
      {0xB0, kB0, sizeof(kB0), 0},
      {0xB1, kB1, sizeof(kB1), 0},
      {0xB2, kB2, sizeof(kB2), 0},
      {0xB3, kB3, sizeof(kB3), 0},
      {0xB5, kB5, sizeof(kB5), 0},
      {0xB7, kB7, sizeof(kB7), 0},
      {0xB8, kB8, sizeof(kB8), 0},
      {0xB9, kB9, sizeof(kB9), 0},
      {0xBB, kBb, sizeof(kBb), 0},
      {0xBC, kBc, sizeof(kBc), 0},
      {0xC1, kC178, sizeof(kC178), 0},
      {0xC2, kC278, sizeof(kC278), 0},
      {0xD0, kD0, sizeof(kD0), 0},
      {0xE0, kE0, sizeof(kE0), 0},
      {0xE1, kE1, sizeof(kE1), 0},
      {0xE2, kE2, sizeof(kE2), 0},
      {0xE3, kE3, sizeof(kE3), 0},
      {0xE4, kE4, sizeof(kE4), 0},
      {0xE5, kE5, sizeof(kE5), 0},
      {0xE6, kE6, sizeof(kE6), 0},
      {0xE7, kE7, sizeof(kE7), 0},
      {0xE8, kE8, sizeof(kE8), 0},
      {0xEB, kEb, sizeof(kEb), 0},
      {0xEC, kEc, sizeof(kEc), 0},
      {0xED, kEd, sizeof(kEd), 0},
      {0xEF, kEf, sizeof(kEf), 0},
      {0xFF, kFf00, sizeof(kFf00), 0},
      {0x11, kNop, sizeof(kNop), 120},  // SLPOUT
      {0x29, kNop, sizeof(kNop), 20},   // DISPON
  };
};

#endif  // TINYGPU_HAS_ESP_LCD_DSI

}  // namespace tinygpu
