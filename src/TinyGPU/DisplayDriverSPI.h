#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <initializer_list>
#include <stdint.h>

#include "DisplayDriver.h"

namespace tinygpu {

/**
 * @brief Common base class for SPI-based display drivers.
 */
template <typename RGB_T = RGB565>
class DisplayDriverSPI : public DisplayDriver<RGB_T> {
 public:
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

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    setAddressWindow(x, y, surface.width(), surface.height());
    spi_.beginTransaction(SPISettings(frequencyHz_, MSBFIRST, SPI_MODE0));
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    // writePixels (not writeBytes!) byte-swaps each 16-bit pixel word to
    // match the panel's expected high-byte-first wire order. RGB_T values
    // are stored in the buffer in the ESP32's native little-endian byte
    // order, so a raw writeBytes() sends each pixel's low byte before its
    // high byte - the two bytes arrive swapped relative to what the
    // controller expects, corrupting color across field boundaries. That
    // silently "rotates" saturated primary colors (only one field
    // populated, so the corruption still lands roughly on a single field)
    // while permanently tinting greys (all three fields populated, so the
    // swap mixes bits across field boundaries with no clean fix at the
    // pixel-format level).
    spi_.writePixels(surface.data(), surface.size());
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
    return true;
  }

  bool readData(ISurface<RGB_T>& surface, size_t x, size_t y) {
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
      surface.setPixel(i % w, i / w, RGB_T(r, g, b));
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

  /// Writes each byte as a separate writeData8() call - a convenience for
  /// sending a command's multi-byte parameter list during init sequences.
  void writeDataN(std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) {
      writeData8(b);
    }
  }
};

/**
 * @brief Driver for ST7735 SPI display controller.
 */
template <typename RGB_T = RGB565>
class ST7735Driver : public DisplayDriverSPI<RGB_T> {
 public:
  using DisplayDriverSPI<RGB_T>::setupPinsAndReset;
  using DisplayDriverSPI<RGB_T>::writeCommand;
  using DisplayDriverSPI<RGB_T>::writeData8;

  ST7735Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI<RGB_T>(spi, cs, dc, rst, 2, 1) {}

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
 */
template <typename RGB_T = RGB565>
class ST7789Driver : public DisplayDriverSPI<RGB_T> {
 public:
  using DisplayDriverSPI<RGB_T>::setupPinsAndReset;
  using DisplayDriverSPI<RGB_T>::writeCommand;
  using DisplayDriverSPI<RGB_T>::writeData8;

  ST7789Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI<RGB_T>(spi, cs, dc, rst, 0, 0) {}

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
 * This init sequence (power control, VCOM, gamma correction, and the
 * leading 0xEF unlock command some clone panels need) and the MADCTL/BGR
 * rotation table are ported from the widely-used, independently
 * maintained Bodmer/TFT_eSPI library's ILI9341 driver - confirmed on
 * real hardware to render standard RGB565 colors correctly (no field
 * compensation needed) on a panel where this library's earlier minimal
 * init (reset/sleep-out/pixel-format/MADCTL/display-on only, with a
 * guessed non-BGR MADCTL) did not, no matter which MADCTL value was
 * tried - the missing piece was the full power/gamma programming, not
 * just the MADCTL bit.
 */
template <typename RGB_T = RGB565>
class ILI9341Driver : public DisplayDriverSPI<RGB_T> {
 public:
  using DisplayDriverSPI<RGB_T>::setupPinsAndReset;
  using DisplayDriverSPI<RGB_T>::writeCommand;
  using DisplayDriverSPI<RGB_T>::writeData8;
  using DisplayDriverSPI<RGB_T>::writeDataN;

  enum class Rotation {
    kNone = -1,
    kPortrait = 0,
    kLandscape = 1,
    kPortraitFlipped = 2,
    kLandscapeFlipped = 3,
  };

  ILI9341Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1,
                Rotation rotation = Rotation::kNone)
      : DisplayDriverSPI<RGB_T>(spi, cs, dc, rst, 0, 0), rotation_(rotation) {}

  bool begin() override {
    setupPinsAndReset();

    writeCommand(0x01);  // SWRESET
    delay(150);

    writeCommand(0xEF);  // Undocumented "unlock" command some clone
    writeDataN({0x03, 0x80, 0x02});  // panels need for correct init

    writeCommand(0xCF);  // Power control B
    writeDataN({0x00, 0xC1, 0x30});

    writeCommand(0xED);  // Power on sequence control
    writeDataN({0x64, 0x03, 0x12, 0x81});

    writeCommand(0xE8);  // Driver timing control A
    writeDataN({0x85, 0x00, 0x78});

    writeCommand(0xCB);  // Power control A
    writeDataN({0x39, 0x2C, 0x00, 0x34, 0x02});

    writeCommand(0xF7);  // Pump ratio control
    writeDataN({0x20});

    writeCommand(0xEA);  // Driver timing control B
    writeDataN({0x00, 0x00});

    writeCommand(0xC0);  // Power control 1
    writeDataN({0x23});

    writeCommand(0xC1);  // Power control 2
    writeDataN({0x10});

    writeCommand(0xC5);  // VCOM control 1
    writeDataN({0x3E, 0x28});

    writeCommand(0xC7);  // VCOM control 2
    writeDataN({0x86});

    writeCommand(0x36);  // MADCTL
    writeDataN({madctlForRotation(rotation_)});

    writeCommand(0x3A);  // Pixel format: 16bpp
    writeDataN({0x55});

    writeCommand(0xB1);  // Frame rate control
    writeDataN({0x00, 0x13});

    writeCommand(0xB6);  // Display function control
    writeDataN({0x08, 0x82, 0x27});

    writeCommand(0xF2);  // Disable 3-gamma
    writeDataN({0x00});

    writeCommand(0x26);  // Gamma set
    writeDataN({0x01});

    writeCommand(0xE0);  // Positive gamma correction
    writeDataN({0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07,
               0x10, 0x03, 0x0E, 0x09, 0x00});

    writeCommand(0xE1);  // Negative gamma correction
    writeDataN({0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08,
               0x0F, 0x0C, 0x31, 0x36, 0x0F});

    writeCommand(0x11);  // Sleep out
    delay(120);
    writeCommand(0x29);  // Display on

    return true;
  }

  void setRotation(Rotation rotation) {
    if (rotation == Rotation::kNone) return;
    rotation_ = rotation;
    writeCommand(0x36);
    writeDataN({madctlForRotation(rotation_)});
  }

  Rotation rotation() const { return rotation_; }

 protected:
  Rotation rotation_;

  // MADCTL values with the BGR bit set (0x08), matching the MX/MY/MV
  // (row/column/exchange) + BGR combinations Bodmer/TFT_eSPI's ILI9341
  // rotation table uses for its 4 primary rotations - not just "BGR set
  // for every rotation", the MX/MY/MV bits differ per rotation too.
  static uint8_t madctlForRotation(Rotation rotation) {
    switch (rotation) {
      case Rotation::kLandscape:
        return 0x28;  // MV | BGR
      case Rotation::kPortraitFlipped:
        return 0x88;  // MY | BGR
      case Rotation::kLandscapeFlipped:
        return 0xE8;  // MX | MY | MV | BGR
      case Rotation::kNone:
      case Rotation::kPortrait:
      default:
        return 0x48;  // MX | BGR
    }
  }
};

/**
 * @brief Driver for ILI9342 SPI display controller.
 *
 * ILI9342 shares ILI9341's command set (this class is structurally
 * identical to ILI9341Driver), but this init sequence additionally
 * programs power control, VCOM and gamma correction registers, which
 * ILI9341Driver's minimal init (reset/sleep-out/pixel-format/MADCTL/
 * display-on only) leaves at the chip's power-on defaults. On some cheap
 * clone panels sold as "ILI9341-compatible" - including ones that may
 * actually be ILI9342 silicon - unprogrammed gamma/power registers are a
 * plausible cause of color/near-black-tint issues that ILI9341Driver's
 * minimal init doesn't fix. If your panel has that kind of issue, try
 * this driver in place of ILI9341Driver; the register values below are a
 * commonly used, well-established init sequence for this chip family,
 * not something tuned to a specific board.
 */
template <typename RGB_T = RGB565>
class ILI9342Driver : public DisplayDriverSPI<RGB_T> {
 public:
  using DisplayDriverSPI<RGB_T>::setupPinsAndReset;
  using DisplayDriverSPI<RGB_T>::writeCommand;
  using DisplayDriverSPI<RGB_T>::writeData8;
  using DisplayDriverSPI<RGB_T>::writeDataN;

  enum class Rotation {
    kNone = -1,
    kPortrait = 0,
    kLandscape = 1,
    kPortraitFlipped = 2,
    kLandscapeFlipped = 3,
  };

  ILI9342Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1,
               Rotation rotation = Rotation::kNone)
      : DisplayDriverSPI<RGB_T>(spi, cs, dc, rst, 0, 0), rotation_(rotation) {}

  bool begin() override {
    setupPinsAndReset();

    writeCommand(0x01);  // SWRESET
    delay(150);

    writeCommand(0xCB);  // Power control B
    writeDataN({0x39, 0x2C, 0x00, 0x34, 0x02});

    writeCommand(0xCF);  // Power on sequence control
    writeDataN({0x00, 0xC1, 0x30});

    writeCommand(0xE8);  // Driver timing control A
    writeDataN({0x85, 0x00, 0x78});

    writeCommand(0xEA);  // Driver timing control B
    writeDataN({0x00, 0x00});

    writeCommand(0xED);  // Power on sequence control
    writeDataN({0x64, 0x03, 0x12, 0x81});

    writeCommand(0xF7);  // Pump ratio control
    writeDataN({0x20});

    writeCommand(0xC0);  // Power control 1
    writeDataN({0x23});

    writeCommand(0xC1);  // Power control 2
    writeDataN({0x10});

    writeCommand(0xC5);  // VCOM control 1
    writeDataN({0x3E, 0x28});

    writeCommand(0xC7);  // VCOM control 2
    writeDataN({0x86});

    writeCommand(0x36);  // MADCTL
    writeDataN({madctlForRotation(rotation_)});

    writeCommand(0x3A);  // Pixel format: 16bpp
    writeDataN({0x55});

    writeCommand(0xB1);  // Frame rate control
    writeDataN({0x00, 0x18});

    writeCommand(0xB6);  // Display function control
    writeDataN({0x08, 0x82, 0x27});

    writeCommand(0xF2);  // Disable 3-gamma
    writeDataN({0x00});

    writeCommand(0x26);  // Gamma set
    writeDataN({0x01});

    writeCommand(0xE0);  // Positive gamma correction
    writeDataN({0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07,
               0x10, 0x03, 0x0E, 0x09, 0x00});

    writeCommand(0xE1);  // Negative gamma correction
    writeDataN({0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08,
               0x0F, 0x0C, 0x31, 0x36, 0x0F});

    writeCommand(0x11);  // Sleep out
    delay(120);
    writeCommand(0x29);  // Display on

    return true;
  }

  void setRotation(Rotation rotation) {
    if (rotation == Rotation::kNone) return;
    rotation_ = rotation;
    writeCommand(0x36);
    writeDataN({madctlForRotation(rotation_)});
  }

  Rotation rotation() const { return rotation_; }

 protected:
  Rotation rotation_;

  static uint8_t madctlForRotation(Rotation rotation) {
    switch (rotation) {
      case Rotation::kLandscape:
        return 0x20;
      case Rotation::kPortraitFlipped:
        return 0x80;
      case Rotation::kLandscapeFlipped:
        return 0xE0;
      case Rotation::kNone:
      case Rotation::kPortrait:
      default:
        return 0x40;
    }
  }
};

/**
 * @brief Driver for HX8357 SPI display controller.
 */
template <typename RGB_T = RGB565>
class HX8357Driver : public DisplayDriverSPI<RGB_T> {
 public:
  using DisplayDriverSPI<RGB_T>::setupPinsAndReset;
  using DisplayDriverSPI<RGB_T>::writeCommand;
  using DisplayDriverSPI<RGB_T>::writeData8;

  HX8357Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI<RGB_T>(spi, cs, dc, rst, 0, 0) {}

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
 */
template <typename RGB_T = RGB565>
class ST7796Driver : public DisplayDriverSPI<RGB_T> {
 public:
  using DisplayDriverSPI<RGB_T>::setupPinsAndReset;
  using DisplayDriverSPI<RGB_T>::writeCommand;
  using DisplayDriverSPI<RGB_T>::writeData8;

  ST7796Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI<RGB_T>(spi, cs, dc, rst, 0, 0) {}

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