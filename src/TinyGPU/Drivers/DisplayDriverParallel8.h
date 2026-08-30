#pragma once
#include <initializer_list>
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Emulation.h"

namespace tinygpu {

/**
 * @brief Common base class for 8-bit parallel ("Intel 8080"-style) TFT
 * display drivers: 8 data lines (D0-D7), WR (write strobe), RD (read
 * strobe, unused - this class only writes), RS/DC (command vs. data) and
 * CS. This is the bus MCUFRIEND-style shields and many 40-pin parallel
 * TFT breakout boards use, wired straight to 8 GPIOs rather than through
 * SPI/QSPI - no dedicated bus peripheral is assumed, every data line and
 * the WR strobe is bit-banged via digitalWrite(), the same portable
 * approach TinyGPU/Input/TouchDriverArduino.h's TouchDriverBitBang uses
 * for a resistive touch panel's four GPIO pins. This trades throughput
 * (one digitalWrite() per bit, per byte) for working unmodified on any
 * Arduino core - a hardware-accelerated 8080 driver (e.g. ESP32's LCD_CAM/
 * esp_lcd_panel_io_i80) would be faster but ties the implementation to one
 * platform's peripheral, the same tradeoff DisplayDriverQSPI.h already
 * makes explicitly for ESP32-only QSPI.
 *
 * The command/parameter protocol on the wire (0x2A/0x2B/0x2C column/row/
 * RAM-write, 0x36 MADCTL, 0x3A pixel format, ...) is identical to the SPI
 * wiring of the same controller families (see DisplayDriverSPI.h) - only
 * how each command/data byte reaches the panel differs, so concrete
 * drivers here mirror DisplayDriverSPI.h's ILI9341Driver/ST7789Driver/...
 * init sequences byte-for-byte.
 */
template <typename RGB_T = RGB565>
class DisplayDriverParallel8 : public DisplayDriver<RGB_T> {
 public:
  /// @param d0..d7 the 8 data lines, D0 = least significant bit.
  /// @param wr write-strobe pin: data is latched by the panel on WR's
  /// rising edge.
  /// @param dc data/command ("RS") select pin: LOW selects command, HIGH
  /// selects data - same polarity/role as DisplayDriverSPI's dc pin.
  /// @param cs chip-select pin, or -1 if the panel has CS permanently
  /// tied low (common on single-panel parallel shields).
  /// @param rd read-strobe pin, or -1 if unused - this class never reads
  /// from the panel, so it only needs to hold RD in its idle (HIGH,
  /// inactive) state so the panel doesn't drive the (shared, bidirectional
  /// on some boards) data bus back.
  /// @param rst reset pin, or -1 if not wired up (e.g. tied to the MCU's
  /// own reset).
  /// @param width/height the panel's addressable resolution, as reported
  /// by width()/height() - subclasses with a rotation concept (e.g.
  /// ILI9341Driver8080) override width()/height() themselves instead and
  /// can ignore these.
  DisplayDriverParallel8(int8_t d0, int8_t d1, int8_t d2, int8_t d3,
                         int8_t d4, int8_t d5, int8_t d6, int8_t d7,
                         int8_t wr, int8_t dc, int8_t cs = -1,
                         int8_t rd = -1, int8_t rst = -1, size_t xOffset = 0,
                         size_t yOffset = 0, size_t width = 240,
                         size_t height = 320)
      : d_{d0, d1, d2, d3, d4, d5, d6, d7},
        wr_(wr),
        dc_(dc),
        cs_(cs),
        rd_(rd),
        rst_(rst),
        xOffset_(xOffset),
        yOffset_(yOffset),
        width_(width),
        height_(height) {}

  size_t width() const override { return width_; }
  size_t height() const override { return height_; }

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    static_assert(sizeof(RGB_T) == 2,
                  "writeData assumes a 16bpp RGB_T (RGB565) stored in "
                  "wire byte order");
    setAddressWindow(x, y, surface.width(), surface.height());
    beginTransaction();
    digitalWrite(dc_, HIGH);
    const uint8_t* src = surface.data();
    const size_t n = surface.size();
    for (size_t i = 0; i < n; ++i) {
      writeByte(src[i]);
    }
    endTransaction();
    return true;
  }

 protected:
  int8_t d_[8];
  int8_t wr_, dc_, cs_, rd_, rst_;
  size_t xOffset_, yOffset_;
  size_t width_, height_;

  void setColumnRowAddress(size_t x, size_t y, size_t w, size_t h) {
    writeCommand(0x2A);
    writeData16(x + xOffset_, x + xOffset_ + w - 1);
    writeCommand(0x2B);
    writeData16(y + yOffset_, y + yOffset_ + h - 1);
  }

  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    setColumnRowAddress(x, y, w, h);
    writeCommand(0x2C);  // RAMWR
    return true;
  }

  void setupPinsAndReset() {
    for (int8_t pin : d_) {
      pinMode(pin, OUTPUT);
    }
    pinMode(wr_, OUTPUT);
    digitalWrite(wr_, HIGH);
    pinMode(dc_, OUTPUT);
    if (cs_ >= 0) {
      pinMode(cs_, OUTPUT);
      digitalWrite(cs_, HIGH);
    }
    if (rd_ >= 0) {
      pinMode(rd_, OUTPUT);
      digitalWrite(rd_, HIGH);  // idle/inactive - this class never reads
    }
    if (rst_ >= 0) {
      pinMode(rst_, OUTPUT);
      digitalWrite(rst_, LOW);
      delay(20);
      digitalWrite(rst_, HIGH);
      delay(150);
    }
  }

  void writeCommand(uint8_t cmd) {
    beginTransaction();
    digitalWrite(dc_, LOW);
    writeByte(cmd);
    endTransaction();
  }

  void writeData16(uint16_t d1, uint16_t d2) {
    beginTransaction();
    digitalWrite(dc_, HIGH);
    writeByte(d1 >> 8);
    writeByte(d1 & 0xFF);
    writeByte(d2 >> 8);
    writeByte(d2 & 0xFF);
    endTransaction();
  }

  void writeData8(uint8_t data) {
    beginTransaction();
    digitalWrite(dc_, HIGH);
    writeByte(data);
    endTransaction();
  }

  /// Writes each byte as a separate writeData8() call - a convenience for
  /// sending a command's multi-byte parameter list during init sequences,
  /// matching DisplayDriverSPI::writeDataN().
  void writeDataN(std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) {
      writeData8(b);
    }
  }

 private:
  void beginTransaction() {
    if (cs_ >= 0) digitalWrite(cs_, LOW);
  }

  void endTransaction() {
    if (cs_ >= 0) digitalWrite(cs_, HIGH);
  }

  /// Drives one byte onto D0-D7 and pulses WR - the panel latches the bus
  /// on WR's rising edge (LOW then HIGH).
  void writeByte(uint8_t data) {
    for (uint8_t i = 0; i < 8; ++i) {
      digitalWrite(d_[i], (data >> i) & 0x01);
    }
    digitalWrite(wr_, LOW);
    digitalWrite(wr_, HIGH);
  }
};

/**
 * @brief Driver for ST7735 8-bit parallel display controller.
 */
template <typename RGB_T = RGB565>
class ST7735Driver8080 : public DisplayDriverParallel8<RGB_T> {
 public:
  using DisplayDriverParallel8<RGB_T>::setupPinsAndReset;
  using DisplayDriverParallel8<RGB_T>::writeCommand;
  using DisplayDriverParallel8<RGB_T>::writeData8;

  ST7735Driver8080(int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t d4,
                   int8_t d5, int8_t d6, int8_t d7, int8_t wr, int8_t dc,
                   int8_t cs = -1, int8_t rd = -1, int8_t rst = -1,
                   size_t width = 128, size_t height = 160)
      : DisplayDriverParallel8<RGB_T>(d0, d1, d2, d3, d4, d5, d6, d7, wr, dc,
                                      cs, rd, rst, 2, 1, width, height) {}

  bool begin() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x3A);
    writeData8(0x05);
    writeCommand(0x29);
    return true;
  }
};

/**
 * @brief Driver for ST7789 8-bit parallel display controller.
 */
template <typename RGB_T = RGB565>
class ST7789Driver8080 : public DisplayDriverParallel8<RGB_T> {
 public:
  using DisplayDriverParallel8<RGB_T>::setupPinsAndReset;
  using DisplayDriverParallel8<RGB_T>::writeCommand;
  using DisplayDriverParallel8<RGB_T>::writeData8;

  ST7789Driver8080(int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t d4,
                   int8_t d5, int8_t d6, int8_t d7, int8_t wr, int8_t dc,
                   int8_t cs = -1, int8_t rd = -1, int8_t rst = -1,
                   size_t width = 240, size_t height = 320)
      : DisplayDriverParallel8<RGB_T>(d0, d1, d2, d3, d4, d5, d6, d7, wr, dc,
                                      cs, rd, rst, 0, 0, width, height) {}

  bool begin() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x3A);
    writeData8(0x55);
    writeCommand(0x29);
    return true;
  }
};

/**
 * @brief Driver for ILI9341 8-bit parallel display controller.
 *
 * Same power/gamma/MADCTL init sequence as DisplayDriverSPI.h's
 * ILI9341Driver (see that class's doc comment for provenance) - only the
 * bus underneath differs.
 */
template <typename RGB_T = RGB565>
class ILI9341Driver8080 : public DisplayDriverParallel8<RGB_T> {
 public:
  using DisplayDriverParallel8<RGB_T>::setupPinsAndReset;
  using DisplayDriverParallel8<RGB_T>::writeCommand;
  using DisplayDriverParallel8<RGB_T>::writeData8;
  using DisplayDriverParallel8<RGB_T>::writeDataN;

  using Rotation = tinygpu::DisplayRotation;

  /// @param nativeWidth/nativeHeight the panel's physical resolution in
  /// its native (portrait, MV bit clear) orientation - 240x320 is the
  /// common ILI9341 module size and the default; pass your panel's real
  /// values if it differs.
  ILI9341Driver8080(int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t d4,
                    int8_t d5, int8_t d6, int8_t d7, int8_t wr, int8_t dc,
                    int8_t cs = -1, int8_t rd = -1, int8_t rst = -1,
                    Rotation rotation = Rotation::kNone,
                    size_t nativeWidth = 240, size_t nativeHeight = 320)
      : DisplayDriverParallel8<RGB_T>(d0, d1, d2, d3, d4, d5, d6, d7, wr, dc,
                                      cs, rd, rst, 0, 0),
        rotation_(rotation),
        nativeWidth_(nativeWidth),
        nativeHeight_(nativeHeight) {}

  size_t width() const override {
    return isLandscapeFamily(rotation_) ? nativeHeight_ : nativeWidth_;
  }
  size_t height() const override {
    return isLandscapeFamily(rotation_) ? nativeWidth_ : nativeHeight_;
  }

  bool begin() override {
    setupPinsAndReset();

    writeCommand(0x01);  // SWRESET
    delay(150);

    writeCommand(0xEF);
    writeDataN({0x03, 0x80, 0x02});

    writeCommand(0xCF);
    writeDataN({0x00, 0xC1, 0x30});

    writeCommand(0xED);
    writeDataN({0x64, 0x03, 0x12, 0x81});

    writeCommand(0xE8);
    writeDataN({0x85, 0x00, 0x78});

    writeCommand(0xCB);
    writeDataN({0x39, 0x2C, 0x00, 0x34, 0x02});

    writeCommand(0xF7);
    writeDataN({0x20});

    writeCommand(0xEA);
    writeDataN({0x00, 0x00});

    writeCommand(0xC0);
    writeDataN({0x23});

    writeCommand(0xC1);
    writeDataN({0x10});

    writeCommand(0xC5);
    writeDataN({0x3E, 0x28});

    writeCommand(0xC7);
    writeDataN({0x86});

    writeCommand(0x36);  // MADCTL
    writeDataN({madctlForRotation(rotation_)});

    writeCommand(0x3A);  // Pixel format: 16bpp
    writeDataN({0x55});

    writeCommand(0xB1);
    writeDataN({0x00, 0x13});

    writeCommand(0xB6);
    writeDataN({0x08, 0x82, 0x27});

    writeCommand(0xF2);
    writeDataN({0x00});

    writeCommand(0x26);
    writeDataN({0x01});

    writeCommand(0xE0);
    writeDataN({0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07,
               0x10, 0x03, 0x0E, 0x09, 0x00});

    writeCommand(0xE1);
    writeDataN({0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08,
               0x0F, 0x0C, 0x31, 0x36, 0x0F});

    writeCommand(0x11);  // Sleep out
    delay(120);
    writeCommand(0x29);  // Display on

    writeCommand(invertColor_ ? 0x21 : 0x20);

    return true;
  }

  void setRotation(Rotation rotation) override {
    if (rotation == Rotation::kNone) return;
    rotation_ = rotation;
    writeCommand(0x36);
    writeDataN({madctlForRotation(rotation_)});
  }

  Rotation rotation() const { return rotation_; }

  /// See DisplayDriverSPI.h's ILI9341Driver::setInvertColor() - same
  /// panel quirk, same fix, just re-sent over this bus's begin() instead.
  void setInvertColor(bool invert) {
    invertColor_ = invert;
    writeCommand(invert ? 0x21 : 0x20);
  }

 protected:
  Rotation rotation_;
  size_t nativeWidth_, nativeHeight_;
  bool invertColor_ = false;

  static uint8_t madctlForRotation(Rotation rotation) {
    switch (rotation) {
      case Rotation::kLandscape:
        return 0x28;
      case Rotation::kPortraitFlipped:
        return 0x88;
      case Rotation::kLandscapeFlipped:
        return 0xE8;
      case Rotation::kNone:
      case Rotation::kPortrait:
      default:
        return 0x48;
    }
  }
};

/**
 * @brief Driver for HX8357 8-bit parallel display controller.
 */
template <typename RGB_T = RGB565>
class HX8357Driver8080 : public DisplayDriverParallel8<RGB_T> {
 public:
  using DisplayDriverParallel8<RGB_T>::setupPinsAndReset;
  using DisplayDriverParallel8<RGB_T>::writeCommand;
  using DisplayDriverParallel8<RGB_T>::writeData8;

  HX8357Driver8080(int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t d4,
                   int8_t d5, int8_t d6, int8_t d7, int8_t wr, int8_t dc,
                   int8_t cs = -1, int8_t rd = -1, int8_t rst = -1,
                   size_t width = 320, size_t height = 480)
      : DisplayDriverParallel8<RGB_T>(d0, d1, d2, d3, d4, d5, d6, d7, wr, dc,
                                      cs, rd, rst, 0, 0, width, height) {}

  bool begin() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x3A);
    writeData8(0x55);
    writeCommand(0x29);
    return true;
  }
};

/**
 * @brief Driver for ST7796 8-bit parallel display controller.
 */
template <typename RGB_T = RGB565>
class ST7796Driver8080 : public DisplayDriverParallel8<RGB_T> {
 public:
  using DisplayDriverParallel8<RGB_T>::setupPinsAndReset;
  using DisplayDriverParallel8<RGB_T>::writeCommand;
  using DisplayDriverParallel8<RGB_T>::writeData8;

  ST7796Driver8080(int8_t d0, int8_t d1, int8_t d2, int8_t d3, int8_t d4,
                   int8_t d5, int8_t d6, int8_t d7, int8_t wr, int8_t dc,
                   int8_t cs = -1, int8_t rd = -1, int8_t rst = -1,
                   size_t width = 320, size_t height = 480)
      : DisplayDriverParallel8<RGB_T>(d0, d1, d2, d3, d4, d5, d6, d7, wr, dc,
                                      cs, rd, rst, 0, 0, width, height) {}

  bool begin() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x3A);
    writeData8(0x55);
    writeCommand(0x36);
    writeData8(0x48);
    writeCommand(0x29);
    return true;
  }
};

}  // namespace tinygpu
