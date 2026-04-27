
#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <stdint.h>

#include "ISurface.h"
#include "DisplayDriver.h"

namespace tinygpu {

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
template <typename RGB_T = RGB565>
class DeviceOutput {
 public:
  /**
   * @brief Construct a DeviceOutput with a display driver.
   * @param driver Pointer to a display driver (ownership not taken)
   * @param width Display width in pixels
   * @param height Display height in pixels
   */
  DeviceOutput(DisplayDriver<RGB_T>& driver)
      : driver_(&driver) {}

  /// Initializes the display and prepares it for receiving data.
  bool begin() {
    if (driver_) {
      return driver_->begin();
    }
    return false;
  }

  void end() {
    if (driver_) {
      driver_->end();
    }
  }

  /// Write a framebuffer to the display (w x h, RGB565, 2 bytes per pixel).
  bool writeData(ISurface<RGB_T>& surface) {
    if (driver_) {
      // write as is
      driver_->writeData(surface);
      return true;
    }
    return false;
  }

 private:
  DisplayDriver<RGB_T>* driver_ = nullptr;
  bool is_active = false;
};

}  // namespace tinygpu
