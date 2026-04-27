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
class DisplayDriverSPI : public DisplayDriver {
 public:
  DisplayDriverSPI(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1,
                   size_t xOffset = 0, size_t yOffset = 0)
      : spi_(spi),
        cs_(cs),
        dc_(dc),
        rst_(rst),
        xOffset_(xOffset),
        yOffset_(yOffset) {}


  bool writeData(ISurface& surface) override {
    setAddressWindow(0, 0, surface.width(), surface.height());
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    spi_.writeBytes(surface.data(), surface.size());
    digitalWrite(cs_, HIGH);
    return true;
  }

 protected:
  SPIClass& spi_;
  int8_t cs_, dc_, rst_;
  size_t xOffset_, yOffset_;

  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    writeCommand(0x2A);
    writeData16(x + xOffset_, x + xOffset_ + w - 1);
    writeCommand(0x2B);
    writeData16(y + yOffset_, y + yOffset_ + h - 1);
    writeCommand(0x2C);
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
    digitalWrite(dc_, LOW);
    digitalWrite(cs_, LOW);
    spi_.transfer(cmd);
    digitalWrite(cs_, HIGH);
  }

  void writeData16(uint16_t d1, uint16_t d2) {
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    spi_.transfer(d1 >> 8);
    spi_.transfer(d1 & 0xFF);
    spi_.transfer(d2 >> 8);
    spi_.transfer(d2 & 0xFF);
    digitalWrite(cs_, HIGH);
  }

  void writeData8(uint8_t data) {
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    spi_.transfer(data);
    digitalWrite(cs_, HIGH);
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
  ILI9341Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : DisplayDriverSPI(spi, cs, dc, rst, 0, 0) {}
  bool begin() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x28);
    writeCommand(0x3A);
    writeData8(0x55);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x29);
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

}  // namespace tinygpu