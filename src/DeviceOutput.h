
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>

#include "ISurface.h"

namespace tinygpu {

/**
 * @brief Abstract base class for display drivers.
 *
 * Interface for all display controller drivers (ST7735, ST7789, ILI9341,
 * HX8357, etc.). All display-specific drivers must implement this interface.
 */
class DisplayDriver {
 public:
  virtual ~DisplayDriver() = default;
  virtual void init() = 0;
  virtual void setAddressWindow(size_t x, size_t y, size_t w, size_t h) = 0;
  virtual void writeData(ISurface& surface) = 0;
};

/**
 * @brief Common base class for SPI-based display drivers.
 *
 * Handles SPI pin setup, hardware reset, address window, and SPI data helpers.
 * Specific display drivers should inherit from this and implement their own
 * init sequence.
 */
class SPIDisplayDriver : public DisplayDriver {
 public:
  SPIDisplayDriver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1,
                   size_t xOffset = 0, size_t yOffset = 0)
      : spi_(spi),
        cs_(cs),
        dc_(dc),
        rst_(rst),
        xOffset_(xOffset),
        yOffset_(yOffset) {}
  void setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    writeCommand(0x2A);
    writeData16(x + xOffset_, x + xOffset_ + w - 1);
    writeCommand(0x2B);
    writeData16(y + yOffset_, y + yOffset_ + h - 1);
    writeCommand(0x2C);
  }
  void writeData(ISurface& surface) override {
    setAddressWindow(0, 0, surface.width(), surface.height());
    digitalWrite(dc_, HIGH);
    digitalWrite(cs_, LOW);
    spi_.writeBytes(surface.data(), surface.size());
    digitalWrite(cs_, HIGH);
  }

 protected:
  SPIClass& spi_;
  int8_t cs_, dc_, rst_;
  size_t xOffset_, yOffset_;
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
class ST7735Driver : public SPIDisplayDriver {
 public:
  ST7735Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : SPIDisplayDriver(spi, cs, dc, rst, 2, 1) {}
  void init() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x3A);
    writeData8(0x05);
    writeCommand(0x29);
  }
};

/**
 * @brief Driver for ST7789 SPI display controller.
 *
 * Handles initialization and address window logic for ST7789 displays.
 */
class ST7789Driver : public SPIDisplayDriver {
 public:
  ST7789Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : SPIDisplayDriver(spi, cs, dc, rst, 0, 0) {}
  void init() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x3A);
    writeData8(0x55);
    writeCommand(0x29);
  }
};

/**
 * @brief Driver for ILI9341 SPI display controller.
 *
 * Handles initialization and address window logic for ILI9341 displays.
 */
class ILI9341Driver : public SPIDisplayDriver {
 public:
  ILI9341Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : SPIDisplayDriver(spi, cs, dc, rst, 0, 0) {}
  void init() override {
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
class HX8357Driver : public SPIDisplayDriver {
 public:
  HX8357Driver(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst = -1)
      : SPIDisplayDriver(spi, cs, dc, rst, 0, 0) {}
  void init() override {
    setupPinsAndReset();
    writeCommand(0x01);
    delay(150);
    writeCommand(0x11);
    delay(120);
    writeCommand(0x3A);
    writeData8(0x55);
    writeCommand(0x29);
  }
};

/**
 * @brief Hardware abstraction for writing framebuffers to SPI-based color
 * displays.
 *
 * DeviceOutput provides a unified interface for sending RGB565 framebuffer data
 * to common embedded display controllers (ST7735, ST7789, ILI9341, HX8357)
 * using the Arduino SPI API.
 *
 * Features:
 * - Handles X/Y offsets for displays that require them (via driver)
 * - Supports configurable color order (RGB/BGR) via RGB565
 * - Initialization sequence and address window logic delegated to driver
 * - Optional hardware reset pin (via driver)
 * - Efficient bulk framebuffer transfer
 *
 * Usage:
 *   DeviceOutput out(new ILI9341Driver(spi, cs, dc, rst), 240, 320);
 *   out.writeData(framebuffer);
 *
 * @note For color order, set RGB565::setColorOrderBGR(true/false) as needed for
 * your display, before creating any data.
 */

/**
 * @brief Hardware abstraction for writing framebuffers to SPI-based color
 * displays.
 *
 * DeviceOutput provides a unified interface for sending RGB565 framebuffer data
 * to common embedded display controllers (ST7735, ST7789, ILI9341, HX8357)
 * using the Arduino SPI API.
 *
 * Features:
 * - Handles X/Y offsets for displays that require them (via driver)
 * - Supports configurable color order (RGB/BGR) via RGB565
 * - Initialization sequence and address window logic delegated to driver
 * - Optional hardware reset pin (via driver)
 * - Efficient bulk framebuffer transfer
 *
 * Usage:
 *   DeviceOutput out(new ILI9341Driver(spi, cs, dc, rst), 240, 320);
 *   out.writeData(framebuffer);
 *
 * @note For color order, set RGB565::setColorOrderBGR(true/false) as needed for
 * your display, before creating any data.
 */
class DeviceOutput {
 public:
  /**
   * @brief Construct a DeviceOutput with a display driver.
   * @param driver Pointer to a display driver (ownership not taken)
   * @param width Display width in pixels
   * @param height Display height in pixels
   */
  DeviceOutput(DisplayDriver* driver, size_t width, size_t height)
      : driver_(driver), width_(width), height_(height) {}

  /// Initializes the display and prepares it for receiving data.
  bool begin() {
    if (driver_) {
      driver_->init();
      return true;
    }
    return false;
  }

  /// Returns the display width in pixels.
  size_t width() const { return width_; }

  /// Returns the display height in pixels.
  size_t height() const { return height_; }

  /// Write a framebuffer to the display (w x h, RGB565, 2 bytes per pixel).
  void writeData(ISurface& surface, size_t x = 0, size_t y = 0) {
    if (driver_) {
      // if the surface size doesn't match the display size, we need to copy it
      // to a temporary buffer that does, because the driver expects a full
      // framebuffer for the address window.
      if (surface.width() != width_ || surface.height() != height_) {
        Sprite temp(surface.width(), surface.height(), surface.font());
        surface.copySprite(x, y, temp);
        driver_->writeData(temp);
        return;
      }
      // write as is
      driver_->writeData(surface);
    }
  }

 private:
  DisplayDriver* driver_ = nullptr;
  size_t width_;
  size_t height_;
  bool is_active = false;

  // SPI helpers and display-specific logic are now in the driver classes.
};

}  // namespace tinygpu
