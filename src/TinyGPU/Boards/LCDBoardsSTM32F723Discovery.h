#pragma once
/**
 * @file LCDBoardsSTM32F723Discovery.h
 * @brief LCDBoard backed by the stm32f723-disco Arduino library
 * (https://github.com/pschatzmann/stm32f723-disco) for the STM32F723E-
 * Discovery board's on-board 240x240 ST7789H2 LCD (wired to the STM32F723
 * via its FMC peripheral in 8080-parallel mode - not SPI/QSPI/DSI) and
 * FT6x06/FT3x67 capacitive touch controller.
 *
 * stm32f723-disco is a separate, optional Arduino library - install it
 * alongside TinyGPU (requires the STM32duino "STMicroelectronics:stm32"
 * core, board "Discovery" / STM32F723E-DISCOVERY, pnum=DISCO_F723IE) and
 * `#include <TinyGPU/Boards/LCDBoardsSTM32F723Discovery.h>` explicitly
 * (this header is not pulled in by LCDBoards.h's platform dispatcher,
 * since it targets one specific STM32 board rather than being a platform
 * default - the same reason LCDBoardsTFTeSPI.h is opt-in) to use it.
 *
 * Deliberately built on the library's own `LCD`/`TouchScreen` wrappers
 * (themselves thin wrappers around ST's STM32Cube BSP `BSP_LCD_*`/
 * `BSP_TS_*` calls) rather than a from-scratch native TinyGPU FMC bus
 * driver: getting the FMC bank/pin/timing mapping and the ST7789H2 init
 * sequence right for this exact board is exactly what that BSP already
 * does, verified by ST for this kit - re-deriving it here without real
 * hardware to test against would risk a driver that's subtly wrong in a
 * way nothing would catch. Same tradeoff LCDBoardsTFTeSPI.h makes by
 * wrapping TFT_eSPI's own `User_Setup.h` instead of re-deriving pin/init
 * tables, and DSIBusSTM32.h makes by taking the DSI PHY PLL config as a
 * pre-computed constructor input instead of guessing board-specific
 * divisors.
 *
 * See LCDBoardsCommon.h for the platform-independent LCDBoard interface
 * this implements.
 */
#include "TinyGPU/Boards/LCDBoardsCommon.h"
#include "TinyGPU/Color/RGB565.h"
#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Input/TouchDriver.h"

#include <STM32F723Discovery.h>

#include <vector>

namespace tinygpu {

/**
 * @brief DisplayDriver<RGB565> wrapping the stm32f723-disco library's
 * `LCD` class - the library owns the panel's init sequence and pixel
 * output entirely (STM32Cube BSP driving the STM32F723's FMC peripheral),
 * so this class is a thin adapter rather than a bus-level driver like
 * DisplayDriverSPI/QSPI/DSI. Fixed 240x240 resolution - the panel is
 * square, so width()/height() don't change with orientation.
 *
 * Byte order: `LCD::drawRGBImage()` expects the conventional (native)
 * RGB565 bit layout - like DisplayDriverSDL's SDL texture upload, this is
 * the opposite of TinyGPU's own stored (SPI-wire-swapped) byte order (see
 * RGB565.h), so writeData() swaps into a scratch buffer before calling
 * drawRGBImage() rather than mutating the surface.
 */
class DisplayDriverSTM32F723Discovery : public DisplayDriver<RGB565> {
 public:
  explicit DisplayDriverSTM32F723Discovery(
      stm32f723::LcdOrientation orientation =
          stm32f723::LcdOrientation::Landscape)
      : orientation_(orientation) {}

  /// Resets and initializes the panel via the stm32f723-disco BSP wrapper.
  bool begin() override { return lcd_.begin(orientation_); }

  size_t width() const override { return kSize; }
  size_t height() const override { return kSize; }

  bool writeData(ISurface<RGB565>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB565>& surface, size_t x, size_t y) override {
    const size_t n = surface.size();
    if (scratch_.size() < n) scratch_.resize(n);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(surface.data());
    uint16_t* dst = reinterpret_cast<uint16_t*>(scratch_.data());
    const size_t pixelCount = n / sizeof(RGB565);
    for (size_t i = 0; i < pixelCount; ++i) {
      dst[i] = RGB565::swapBytes(src[i]);
    }
    lcd_.drawRGBImage(static_cast<uint16_t>(x), static_cast<uint16_t>(y),
                      static_cast<uint16_t>(surface.width()),
                      static_cast<uint16_t>(surface.height()),
                      scratch_.data());
    return true;
  }

  /// Direct access to the underlying stm32f723::LCD instance - e.g. for
  /// displayOn()/displayOff() or the library's own text/shape drawing,
  /// which this driver doesn't wrap.
  stm32f723::LCD& lcd() { return lcd_; }

 protected:
  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    return true;
  }

 private:
  static constexpr size_t kSize = 240;

  stm32f723::LcdOrientation orientation_;
  stm32f723::LCD lcd_;
  std::vector<uint8_t> scratch_;
};

/**
 * @brief TouchDriver wrapping the stm32f723-disco library's `TouchScreen`
 * class (on-board FT6x06/FT3x67 capacitive touch controller - bus/pin
 * setup is entirely owned by the library, same as the LCD side).
 */
class TouchDriverSTM32F723Discovery : public TouchDriver {
 public:
  /// Initializes the touch controller, scaled to the panel's 240x240
  /// resolution (see DisplayDriverSTM32F723Discovery).
  bool begin() override { return touch_.begin(kSize, kSize); }

  bool isTouched() override { return touch_.read().touched(); }

  bool getPoint(Point& outPoint) override {
    const stm32f723::TouchState state = touch_.read();
    if (!state.touched()) return false;
    outPoint.x = state.primary().x;
    outPoint.y = state.primary().y;
    outPoint.pressure = 255;
    return true;
  }

  /// The FT6x06/FT3x67 supports up to two simultaneous touches.
  bool getSecondPoint(Point& outPoint) override {
    const stm32f723::TouchState state = touch_.read();
    if (state.touchCount < 2) return false;
    outPoint.x = state.points[1].x;
    outPoint.y = state.points[1].y;
    outPoint.pressure = 255;
    return true;
  }

 private:
  static constexpr uint16_t kSize = 240;

  stm32f723::TouchScreen touch_;
};

/**
 * @brief "STM32F723E-Discovery" - on-board 240x240 ST7789H2 LCD (FMC) +
 * FT6x06/FT3x67 capacitive touch, via the stm32f723-disco library.
 *
 * No I2S/LED pin assignment here: the board's audio codec is driven over
 * SAI (see the library's own Audio.h), not I2S, so i2s() reports "not
 * present" (every field -1); LED_RED/LED_GREEN/USER_BTN are ordinary
 * Arduino digital pins defined by the STM32duino board variant itself
 * (see the stm32f723-disco umbrella header's doc comment) rather than
 * pins this class would need to track, so led() likewise reports "not
 * present".
 */
class LCDBoardSTM32F723Discovery : public LCDBoard {
 public:
  /// Initializes the display, then the touch controller.
  bool begin() override {
    if (!display_.begin()) return false;
    return touch_.begin();
  }

  size_t width() const override { return display_.width(); }
  size_t height() const override { return display_.height(); }

  DisplayDriverSTM32F723Discovery& display() override { return display_; }
  TouchDriver* touch() override { return &touch_; }
  const I2SPins& i2s() const override { return i2s_; }
  const LEDPins& led() const override { return led_; }

 private:
  DisplayDriverSTM32F723Discovery display_;
  TouchDriverSTM32F723Discovery touch_;
  I2SPins i2s_{};
  LEDPins led_{};
};

using STM32F723Discovery = LCDBoardSTM32F723Discovery;

}  // namespace tinygpu
