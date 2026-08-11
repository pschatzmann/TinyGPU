#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>

#include "DisplayDriver.h"

namespace tinygpu {

/**
 * @brief Common base class for SPI-based display drivers.
 *
 * Handles SPI pin setup, hardware reset, address window, and SPI data helpers.
 * Specific display drivers should inherit from this and implement their own
 * init sequence.
 */
class DisplayDriverSPI : public DisplayDriver<RGB565> {
 public:
  /**
   * @param frequencyHz SPI clock speed for commands and pixel data writes.
   * Defaults to 40 MHz, a commonly-supported safe max for ILI9341/ST77xx
   * panels. Lower it (e.g. 20000000 or 10000000) if you see glitches, which
   * are more likely over long/breadboard wiring.
   * @param readFrequencyHz SPI clock speed for readData(). GRAM readback is
   * driven weaker than writes on most of these controllers and needs a
   * slower clock to be reliable; defaults to 15 MHz.
   */
  DisplayDriverSPI(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1,
                   size_t xOffset = 0, size_t yOffset = 0,
                   uint32_t frequencyHz = 40000000,
                   uint32_t readFrequencyHz = 15000000)
      : spi_(spi),
        cs_(cs),
        dc_(dc),
        rst_(rst),
        xOffset_(xOffset),
        yOffset_(yOffset),
        frequencyHz_(frequencyHz),
        readFrequencyHz_(readFrequencyHz) {}


  bool writeData(ISurface<RGB565>& surface) override {
    return writeData(surface, 0, 0);
  }

  /**
   * @brief Writes a sub-region ("band") of pixel data starting at display
   * coordinates (x, y).
   *
   * Useful on boards without enough contiguous RAM for a full-screen
   * framebuffer: render a small band-sized surface and stream it to the
   * display repeatedly at increasing y offsets instead of allocating one
   * large buffer.
   */
  bool writeData(ISurface<RGB565>& surface, size_t x, size_t y) {
    setAddressWindow(x, y, surface.width(), surface.height());
    spi_.beginTransaction(SPISettings(frequencyHz_, MSBFIRST, SPI_MODE0));
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    spi_.writeBytes(surface.data(), surface.size());
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
    return true;
  }

  /**
   * @brief Reads a rectangular region of pixels back from the display
   * controller's own GRAM into `surface`, so a "sprite" background can be
   * captured/restored without keeping a full local framebuffer.
   *
   * Uses the RAMRD (0x2E) command shared by most ILI9341/ST77xx-family
   * controllers: after setting the column/row address window, the panel
   * clocks out one dummy byte followed by 3 bytes per pixel (R, G, B, each
   * with the color value in the top bits). This is read at a slower clock
   * than writes (see readFrequencyHz in the constructor), since GRAM
   * readback is weaker/less reliable at high speed on most of these parts.
   *
   * CS is held low continuously from the column/row address commands
   * through the RAMRD command and every data byte - most of these
   * controllers only return real GRAM data (rather than zeros/garbage) if
   * that whole sequence happens in one unbroken chip-select period.
   *
   * @note Requires MISO to be wired to the panel. Some controller clones
   * differ in their exact read timing/format or don't support reliable
   * readback at all - verify this works correctly on your specific
   * hardware before relying on it (e.g. read back a region you just wrote
   * a known color to, and confirm the color matches).
   */
  bool readData(ISurface<RGB565>& surface, size_t x, size_t y) {
    size_t w = surface.width();
    size_t h = surface.height();
    size_t x0 = x + xOffset_;
    size_t y0 = y + yOffset_;

    spi_.beginTransaction(SPISettings(readFrequencyHz_, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);

    digitalWrite(dc_, LOW);
    spi_.transfer(0x2A);
    digitalWrite(dc_, HIGH);
    spi_.transfer((x0) >> 8);
    spi_.transfer((x0) & 0xFF);
    spi_.transfer((x0 + w - 1) >> 8);
    spi_.transfer((x0 + w - 1) & 0xFF);

    digitalWrite(dc_, LOW);
    spi_.transfer(0x2B);
    digitalWrite(dc_, HIGH);
    spi_.transfer((y0) >> 8);
    spi_.transfer((y0) & 0xFF);
    spi_.transfer((y0 + h - 1) >> 8);
    spi_.transfer((y0 + h - 1) & 0xFF);

    digitalWrite(dc_, LOW);
    spi_.transfer(0x2E);
    digitalWrite(dc_, HIGH);

    spi_.transfer(0);  // leading dummy byte before real pixel data
    size_t pixelCount = w * h;
    for (size_t i = 0; i < pixelCount; ++i) {
      uint8_t r = spi_.transfer(0);
      uint8_t g = spi_.transfer(0);
      uint8_t b = spi_.transfer(0);
      surface.setPixel(i % w, i / w, RGB565(r, g, b));
    }

    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
    return true;
  }

 protected:
  SPIClass& spi_;
  int8_t cs_, dc_, rst_;
  size_t xOffset_, yOffset_;
  uint32_t frequencyHz_;
  uint32_t readFrequencyHz_;

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
    pinMode(cs_, OUTPUT);
    pinMode(dc_, OUTPUT);
    if (rst_ >= 0) {
      pinMode(rst_, OUTPUT);
      digitalWrite(rst_, LOW);
      delay(20);
      digitalWrite(rst_, HIGH);
      delay(150);
    }
    digitalWrite(cs_, HIGH);
  }

  void writeCommand(uint8_t cmd) {
    spi_.beginTransaction(SPISettings(frequencyHz_, MSBFIRST, SPI_MODE0));
    digitalWrite(dc_, LOW);
    digitalWrite(cs_, LOW);
    spi_.transfer(cmd);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
  }

  void writeData16(uint16_t d1, uint16_t d2) {
    spi_.beginTransaction(SPISettings(frequencyHz_, MSBFIRST, SPI_MODE0));
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    spi_.transfer(d1 >> 8);
    spi_.transfer(d1 & 0xFF);
    spi_.transfer(d2 >> 8);
    spi_.transfer(d2 & 0xFF);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
  }

  void writeData8(uint8_t data) {
    spi_.beginTransaction(SPISettings(frequencyHz_, MSBFIRST, SPI_MODE0));
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    spi_.transfer(data);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
  }
};

/**
 * @brief Driver for ST7735 SPI display controller.
 *
 * Handles initialization and address window logic for ST7735 displays.
 */
class ST7735Driver : public DisplayDriverSPI {
 public:
  ST7735Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI(spi, cs, dc, rst, 2, 1) {}
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
 * @brief Driver for ST7789 SPI display controller.
 *
 * Handles initialization and address window logic for ST7789 displays.
 */
class ST7789Driver : public DisplayDriverSPI {
 public:
  ST7789Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI(spi, cs, dc, rst, 0, 0) {}
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
 * @brief Driver for ILI9341 SPI display controller.
 *
 * Handles initialization and address window logic for ILI9341 displays.
 */
class ILI9341Driver : public DisplayDriverSPI {
 public:
  /**
   * @brief Display orientation, sent via the MADCTL command (0x36) -
   * same convention as Adafruit_ILI9341::setRotation(). kNone means
   * "don't send MADCTL at all" - the panel's power-on-reset default
   * orientation, preserving this driver's original (pre-rotation-support)
   * behavior exactly for existing code that doesn't request a rotation.
   */
  enum class Rotation {
    kNone = -1,
    kPortrait = 0,
    kLandscape = 1,
    kPortraitFlipped = 2,
    kLandscapeFlipped = 3,
  };

  ILI9341Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1,
                Rotation rotation = Rotation::kNone)
      : DisplayDriverSPI(spi, cs, dc, rst, 0, 0), rotation_(rotation) {}

  bool begin() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x28);
    writeCommand(0x3A);
    writeData8(0x55);
    if (rotation_ != Rotation::kNone) {
      writeCommand(0x36);
      writeData8(madctlForRotation(rotation_));
    }
    writeCommand(0x11);
    delay(120);
    writeCommand(0x29);
    return true;
  }

  /**
   * @brief Changes the display orientation after begin() - sends MADCTL
   * (0x36) directly, so it takes effect immediately without a full
   * re-init. Rotation::kNone is ignored.
   */
  void setRotation(Rotation rotation) {
    if (rotation == Rotation::kNone) return;
    rotation_ = rotation;
    writeCommand(0x36);
    writeData8(madctlForRotation(rotation_));
  }

  /// The rotation passed to the constructor or setRotation(), or
  /// Rotation::kNone if neither has been called (MADCTL was never sent).
  Rotation rotation() const { return rotation_; }

 protected:
  Rotation rotation_;

  /// Adafruit_ILI9341-compatible MADCTL byte for `rotation`.
  static uint8_t madctlForRotation(Rotation rotation) {
    switch (rotation) {
      case Rotation::kPortrait:
        return 0x48;  // portrait: MX | BGR
      case Rotation::kLandscape:
        return 0x28;  // landscape: MV | BGR
      case Rotation::kPortraitFlipped:
        return 0x88;  // portrait, flipped 180: MY | BGR
      default:
        return 0xE8;  // landscape, flipped 180: MX | MY | MV | BGR
    }
  }
};

/**
 * @brief Driver for HX8357 SPI display controller.
 *
 * Handles initialization and address window logic for HX8357 displays.
 */
class HX8357Driver : public DisplayDriverSPI {
 public:
  HX8357Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI(spi, cs, dc, rst, 0, 0) {}
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
 * @brief Driver for ST7796 SPI display controller.
 *
 * Commonly found on 3.5" 320x480 (and cropped 240x320) TFT touch boards,
 * e.g. the "ESP32 LVGL WIFI&Bluetooth" style development boards.
 * Handles initialization and address window logic for ST7796 displays.
 */
class ST7796Driver : public DisplayDriverSPI {
 public:
  ST7796Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI(spi, cs, dc, rst, 0, 0) {}
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