#pragma once
#include <Arduino.h>
#include <SPI.h>
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
    spi_.writeBytes(surface.data(), surface.size());
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
 */
template <typename RGB_T = RGB565>
class ILI9341Driver : public DisplayDriverSPI<RGB_T> {
 public:
  using DisplayDriverSPI<RGB_T>::setupPinsAndReset;
  using DisplayDriverSPI<RGB_T>::writeCommand;
  using DisplayDriverSPI<RGB_T>::writeData8;

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
    writeCommand(0x01);
    delay(150);
    writeCommand(0x28);
    writeCommand(0x3A);
    writeData8(0x55);
    writeCommand(0x36);
    writeData8(madctlForRotation(rotation_));
    writeCommand(0x11);
    delay(120);
    writeCommand(0x29);
    return true;
  }

  void setRotation(Rotation rotation) {
    if (rotation == Rotation::kNone) return;
    rotation_ = rotation;
    writeCommand(0x36);
    writeData8(madctlForRotation(rotation_));
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