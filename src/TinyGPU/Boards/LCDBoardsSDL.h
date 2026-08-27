#pragma once
/**
 * @file LCDBoardsSDL.h
 * @brief LCDBoard for the SDL2 desktop backend - renders to a window via
 * DisplayDriverSDL and maps the mouse to touch via TouchDriverSDL, so
 * sketches written against the LCDBoard interface can run unmodified on a
 * desktop for testing/debugging without any physical hardware.
 *
 * See LCDBoardsCommon.h for the platform-independent LCDBoard interface this
 * implements, and LCDBoardsESP32.h for the ESP32 hardware boards.
 */
#include "TinyGPU/Boards/LCDBoardsCommon.h"
#include "TinyGPU/Drivers/DisplayDriverSDL.h"
#include "TinyGPU/Input/TouchDriverSDL.h"

namespace tinygpu {

/**
 * @brief Desktop SDL2 window standing in for a physical LCD board. Window
 * size is set at construction time (default 320x240) since - unlike the
 * hardware boards in LCDBoardsESP32.h - there is no fixed panel to match.
 *
 * The left mouse button stands in for a finger (see TouchDriverSDL).
 * There is no backlight/I2S/LED hardware on this backend, so i2s()/led()
 * report "not present" (every field -1) and backlightPin() is -1.
 */
class LCDBoardDesktopSDL : public LCDBoard {
 public:
  LCDBoardDesktopSDL(size_t width = 320, size_t height = 240)
      : width_(width), height_(height), display_(width, height) {}

  /// Opens the SDL window and starts mouse-as-touch tracking.
  bool begin() override {
    if (!display_.begin()) return false;
    return touch_.begin();
  }

  /// Window width in pixels.
  size_t width() const override { return width_; }
  /// Window height in pixels.
  size_t height() const override { return height_; }

  /// The board's display driver.
  DisplayDriverSDL<RGB565>& display() override { return display_; }
  /// The board's touch controller (mouse-mapped).
  TouchDriver* touch() override { return &touch_; }
  /// No I2S bus on this backend - every field is -1.
  const I2SPins& i2s() const override { return i2s_; }
  /// No LED on this backend - every field is -1.
  const LEDPins& led() const override { return led_; }

 private:
  size_t width_;
  size_t height_;
  DisplayDriverSDL<RGB565> display_;
  TouchDriverSDL touch_;
  I2SPins i2s_{};
  LEDPins led_{};
};

using DesktopSDL = LCDBoardDesktopSDL;

}  // namespace tinygpu
