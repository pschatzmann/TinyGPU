#pragma once
/**
 * @file LCDBoardsTFTeSPI.h
 * @brief LCDBoard backed by Bodmer's TFT_eSPI
 * (https://github.com/Bodmer/TFT_eSPI) instead of one of this library's
 * own SPI panel/touch drivers - useful if you already have a working
 * TFT_eSPI `User_Setup.h` for your panel (and optionally its touch
 * calibration) and would rather reuse it than re-wire the same panel
 * through LCDBoardsESP32.h's board-specific pin tables.
 *
 * TFT_eSPI is a separate, optional Arduino library - install it alongside
 * TinyGPU and `#include <TinyGPU/Boards/LCDBoardsTFTeSPI.h>` explicitly
 * (this header is not pulled in by LCDBoards.h's platform dispatcher,
 * since TFT_eSPI is a deliberate choice rather than a platform default -
 * it can just as well be used on an ESP32 board that would otherwise get
 * LCDBoardsESP32.h) to opt in.
 *
 * See LCDBoardsCommon.h for the platform-independent LCDBoard interface
 * this implements, and DisplayDriverTFTeSPI.h/TouchDriverTFTeSPI.h for the
 * two drivers it bundles.
 */
#include "TinyGPU/Boards/LCDBoardsCommon.h"
#include "TinyGPU/Drivers/DisplayDriverTFTeSPI.h"
#include "TinyGPU/Input/TouchDriverTFTeSPI.h"

namespace tinygpu {

/**
 * @brief LCDBoard wrapping a single TFT_eSPI instance, shared between its
 * DisplayDriverTFTeSPI and (optional) TouchDriverTFTeSPI.
 *
 * All panel-specific configuration (controller, pins, SPI frequency, ...)
 * is TFT_eSPI's own `User_Setup.h`/`User_Setup_Select.h` job, same as any
 * other TFT_eSPI sketch - width/height are still passed to the
 * constructor (rather than read from TFT_eSPI) because, like every other
 * LCDBoard here, callers may need them before begin() runs.
 *
 * Touch is opt-in via the constructor's `hasTouch` flag (many TFT_eSPI
 * panels have no touch hardware at all) - touch() returns nullptr when
 * disabled. When enabled, use tft() to reach TFT_eSPI's own touch
 * calibration (tft().setTouch(calData), from tft().calibrateTouch(...)
 * or a hardcoded panel-specific array) before calling begin() - TFT_eSPI
 * owns touch calibration entirely, the same "driver does not own
 * bus/calibration setup, the application does" split
 * TouchDriverArduino.h's drivers use for their own SPI/I2C buses.
 *
 * No backlight/I2S/LED pin handling here - TFT_eSPI's own `User_Setup.h`
 * covers the backlight pin (`TFT_BL`) if your panel needs one driven
 * manually; i2s()/led() report "not present" (every field -1) and
 * backlightPin() is -1, the same as LCDBoardDesktopSDL.
 */
class LCDBoardTFTeSPI : public LCDBoard {
 public:
  explicit LCDBoardTFTeSPI(size_t width, size_t height, bool hasTouch = false,
                           uint16_t touchThreshold = 600)
      : width_(width),
        height_(height),
        display_(tft_),
        touch_(tft_, touchThreshold),
        hasTouch_(hasTouch) {}

  /// Initializes TFT_eSPI (via DisplayDriverTFTeSPI::begin()) and, if
  /// `hasTouch` was set, the touch driver.
  bool begin() override {
    if (!display_.begin()) return false;
    if (hasTouch_) return touch_.begin();
    return true;
  }

  /// Panel width in pixels, as passed to the constructor.
  size_t width() const override { return width_; }
  /// Panel height in pixels, as passed to the constructor.
  size_t height() const override { return height_; }

  /// The board's display driver.
  DisplayDriverTFTeSPI<RGB565>& display() override { return display_; }
  /// The board's touch controller, or nullptr if `hasTouch` was false.
  TouchDriver* touch() override { return hasTouch_ ? &touch_ : nullptr; }
  /// No I2S bus tracked here - every field is -1.
  const I2SPins& i2s() const override { return i2s_; }
  /// No LED tracked here - every field is -1.
  const LEDPins& led() const override { return led_; }

  /// Direct access to the underlying TFT_eSPI instance - e.g. for touch
  /// calibration (see class comment) or any TFT_eSPI feature this driver
  /// doesn't wrap.
  TFT_eSPI& tft() { return tft_; }

 private:
  TFT_eSPI tft_;  // must be declared (and so constructed) before display_/
                  // touch_ below, which bind references to it.
  size_t width_;
  size_t height_;
  DisplayDriverTFTeSPI<RGB565> display_;
  TouchDriverTFTeSPI touch_;
  bool hasTouch_;
  I2SPins i2s_{};
  LEDPins led_{};
};

}  // namespace tinygpu
