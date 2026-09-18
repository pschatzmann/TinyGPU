#pragma once
/**
 * @file LCDBoardsSTM32.h
 * @brief One-call setup for STM32duino boards that bundle an LCD, following
 * the same LCDBoard interface as LCDBoardsESP32.h's ESP32 boards.
 *
 * See LCDBoards.h for the platform-independent LCDBoard interface these
 * classes implement.
 */
#include "TinyGPU/Emulation.h"

#if defined(ARDUINO_ARCH_STM32)

// Unlike ESP32 (SPI.h/Wire.h are part of the core itself), STM32duino
// ships both as separate libraries - arduino-cli only adds a library to
// the include path when some file textually #includes it, which
// Emulation.h's __has_include check alone doesn't trigger (that's a
// compile-time check, after arduino-cli's library dependency resolution
// has already run). TouchDriverArduino.h (pulled in transitively by
// TinyGPU.h, unconditionally on every platform) needs SPIClass/TwoWire
// declared even though this board has no touch controller of its own.
#include <SPI.h>
#include <Wire.h>
#include "TinyGPU/Boards/LCDBoardsCommon.h"
#include "TinyGPU/Drivers/DisplayDriver.h"

namespace tinygpu {

/**
 * @brief Hardware-SPI4 ST7735 driver for the WeAct MiniSTM32H7xx's bundled
 * 0.96" 160x80 TFT.
 *
 * Getting this panel working took a few rounds of hardware debugging:
 *  - Passing NC as SPIClass's MISO pin hangs the STM32 core's
 *    spi_transfer() forever (TXP/RXP flag never sets) for SPI4 on this
 *    board's custom PeripheralPins variant - worked around by giving it a
 *    real but physically-unwired MISO pin instead (this panel has no
 *    MISO/SDO line).
 *  - The backlight (see LCDBoardWeActMiniSTM32H750::begin()) needs
 *    analogWrite() PWM, not a plain digital HIGH.
 *  - This panel needs CS held low continuously across a command byte and
 *    its data bytes (see writeCommand()'s comment) - closing and
 *    reopening CS between them makes it misparse the data as a new
 *    command instead of a parameter.
 *  - It needs Adafruit_ST7735's INITR_MINI160x80_PLUGIN init parameters
 *    (colstart/rowstart/MADCTL/INVON - see begin() and
 *    LCDBoardWeActMiniSTM32H750's doc comment), not the plain
 *    INITR_MINI160x80 tab.
 * With all of those fixed, hardware SPI4 works reliably and is much
 * faster than a bit-banged fallback would be.
 */
class ST7735DriverHardwareSPI : public DisplayDriver<RGB565> {
 public:
  /// @param frequencyHz SPI clock - 15MHz default matches WeAct's own
  /// factory firmware for this exact panel (SPI4 at APB1/8, ~120MHz/8),
  /// itself in line with the ST7735's typical 15-20MHz datasheet ceiling.
  ST7735DriverHardwareSPI(SPIClass& spi, int8_t cs, int8_t dc, int8_t rst,
                          size_t xOffset, size_t yOffset, size_t width,
                          size_t height, uint8_t madctl,
                          uint32_t frequencyHz = 15000000)
      : spi_(spi),
        cs_(cs),
        dc_(dc),
        rst_(rst),
        xOffset_(xOffset),
        yOffset_(yOffset),
        width_(width),
        height_(height),
        madctl_(madctl),
        frequencyHz_(frequencyHz) {}

  size_t width() const override { return width_; }
  size_t height() const override { return height_; }

  bool begin() override {
    pinMode(cs_, OUTPUT);
    pinMode(dc_, OUTPUT);
    digitalWrite(cs_, HIGH);
    spi_.begin();

    if (rst_ >= 0) {
      pinMode(rst_, OUTPUT);
      digitalWrite(rst_, HIGH);
      delay(5);
      digitalWrite(rst_, LOW);
      delay(20);
      digitalWrite(rst_, HIGH);
      delay(150);
    }

    beginTransaction();
    writeCommand(0x01);  // SWRESET, no data
    endTransaction();
    delay(150);
    beginTransaction();
    writeCommand(0x11);  // SLPOUT, no data
    endTransaction();
    delay(120);
    beginTransaction();
    writeCommand(0x21);  // INVON, no data - this panel is ribbon/FPC-
                         // mounted (fold-and-tape onto the core board),
                         // matching Adafruit_ST7735's
                         // INITR_MINI160x80_PLUGIN variant, which
                         // explicitly inverts - not the plain
                         // INITR_MINI160x80 tab (INVOFF).
    uint8_t colmod = 0x05;
    writeCommand(0x3A, &colmod, 1);  // COLMOD: 16bpp
    uint8_t madctlData = madctl_;
    writeCommand(0x36, &madctlData, 1);  // MADCTL
    writeCommand(0x29);                  // DISPON, no data
    endTransaction();
    return true;
  }

  bool writeData(ISurface<RGB565>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB565>& surface, size_t x, size_t y) override {
    beginTransaction();
    setAddressWindow(x, y, surface.width(), surface.height());
    // setAddressWindow() has already sent RAMWR (0x2C) and left CS low/DC
    // high, deliberately not closing that transaction - the pixel bytes
    // below are RAMWR's data and must share its CS assertion (see
    // writeCommand()'s comment for why).
    const uint8_t* src = surface.data();
    const size_t n = surface.size();
    // Sent in chunks through a small scratch buffer, like
    // DisplayDriverSPI::writeData() - SPIClass::transfer(buf, count) is
    // full-duplex and overwrites buf with received data, which would
    // corrupt surface's own buffer if passed directly.
    constexpr size_t kChunkBytes = 256;
    uint8_t chunk[kChunkBytes];
    size_t remaining = n;
    const uint8_t* p = src;
    while (remaining > 0) {
      const size_t c = remaining < kChunkBytes ? remaining : kChunkBytes;
      memcpy(chunk, p, c);
      spi_.transfer(chunk, c);
      p += c;
      remaining -= c;
    }
    digitalWrite(cs_, HIGH);  // closes the RAMWR + pixel-data transaction
    endTransaction();
    return true;
  }

 protected:
  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    uint8_t caset[4] = {
        static_cast<uint8_t>((x + xOffset_) >> 8),
        static_cast<uint8_t>((x + xOffset_) & 0xFF),
        static_cast<uint8_t>((x + xOffset_ + w - 1) >> 8),
        static_cast<uint8_t>((x + xOffset_ + w - 1) & 0xFF)};
    writeCommand(0x2A, caset, 4);  // CASET
    uint8_t raset[4] = {
        static_cast<uint8_t>((y + yOffset_) >> 8),
        static_cast<uint8_t>((y + yOffset_) & 0xFF),
        static_cast<uint8_t>((y + yOffset_ + h - 1) >> 8),
        static_cast<uint8_t>((y + yOffset_ + h - 1) & 0xFF)};
    writeCommand(0x2B, raset, 4);  // RASET

    // RAMWR: deliberately does NOT close CS - see writeData()'s comment.
    digitalWrite(cs_, LOW);
    digitalWrite(dc_, LOW);
    spi_.transfer(static_cast<uint8_t>(0x2C));
    digitalWrite(dc_, HIGH);
    return true;
  }

 private:
  SPIClass& spi_;
  int8_t cs_, dc_, rst_;
  size_t xOffset_, yOffset_, width_, height_;
  uint8_t madctl_;
  uint32_t frequencyHz_;

  void beginTransaction() {
    spi_.beginTransaction(SPISettings(frequencyHz_, MSBFIRST, SPI_MODE0));
  }
  void endTransaction() { spi_.endTransaction(); }

  /// Sends a command byte and (optionally) its data bytes under one
  /// continuous CS assertion - matching Adafruit_SPITFT::sendCommand()
  /// (see Adafruit_SPITFT.cpp), which this panel needs: closing CS
  /// between the command and its data, then reopening it for the data,
  /// makes this panel misparse the data bytes as new command opcodes
  /// instead of parameters - confirmed on real hardware (deterministic,
  /// wrong-but-consistent colors, unaffected by widening this driver's
  /// SPI clock's timing margins, which ruled out a signal-integrity
  /// explanation before this framing bug was found). Assumes a
  /// beginTransaction() is already active (begin()/writeData() bracket
  /// their own calls to this).
  void writeCommand(uint8_t cmd, const uint8_t* data = nullptr,
                    size_t len = 0) {
    digitalWrite(cs_, LOW);
    digitalWrite(dc_, LOW);
    spi_.transfer(cmd);
    if (len > 0) {
      digitalWrite(dc_, HIGH);
      for (size_t i = 0; i < len; ++i) {
        spi_.transfer(data[i]);
      }
    }
    digitalWrite(cs_, HIGH);
  }
};

/**
 * @brief WeAct "MiniSTM32H7xx" core board (STM32H750VBT6/STM32H743VIT6)
 * with its bundled, ribbon/FPC-mounted 0.96" 160x80 ST7735 TFT, mounted
 * landscape:
 * https://github.com/WeActStudio/MiniSTM32H7xx
 *
 * Display: ST7735, 160x80, hardware SPI4 (see ST7735DriverHardwareSPI's
 *          doc comment for the earlier bit-banged detour and why hardware
 *          SPI4 turned out fine after all) - CS=PE11 DC(RS)=PE13 SCK=PE12
 *          MOSI=PE14 RST=PE15; BL=PE10 (TIM1_CH2N, PWM-capable - driven
 *          via analogWrite() by begin(); a plain digital HIGH left the
 *          backlight dark on real hardware, see backlightPin() for
 *          further brightness control).
 * Touch/I2S/LED: none on this board.
 *
 * Panel offsets (xOffset=1, yOffset=26) and MADCTL (0xA8 = MY|MV|BGR)
 * match Adafruit_ST7735's INITR_MINI160x80_PLUGIN tab + setRotation(1) -
 * the variant meant for ribbon/FPC-mounted mini panels like this one, as
 * opposed to a directly-soldered module - confirmed working on real
 * hardware, including its need for INVON (see
 * ST7735DriverHardwareSPI::begin()) where the plain (non-plugin)
 * INITR_MINI160x80 tab uses INVOFF.
 */
class LCDBoardWeActMiniSTM32H750 : public LCDBoard {
 public:
  /// Sets up the backlight and display controller. Returns false if the
  /// display begin() fails.
  bool begin() override {
    pinMode(kPinBacklight, OUTPUT);
    analogWrite(kPinBacklight, 180);

    return display_.begin();
  }

  /// Panel width in pixels (landscape).
  size_t width() const override { return 160; }
  /// Panel height in pixels (landscape).
  size_t height() const override { return 80; }

  /// The board's display driver.
  ST7735DriverHardwareSPI& display() override { return display_; }
  /// This board has no touch controller.
  TouchDriver* touch() override { return nullptr; }
  /// This board has no I2S bus - every I2SPins field is -1.
  const I2SPins& i2s() const override { return i2s_; }
  /// This board has no RGB LED - every LEDPins field is -1.
  const LEDPins& led() const override { return led_; }
  /// The board's backlight GPIO pin.
  int8_t backlightPin() const override { return kPinBacklight; }

 private:
  static constexpr int8_t kPinCs = PE11;
  static constexpr int8_t kPinDc = PE13;
  static constexpr int8_t kPinRst = PE15;
  static constexpr int8_t kPinSck = PE12;
  static constexpr int8_t kPinMosi = PE14;
  static constexpr int8_t kPinBacklight = PE10;
  // Not physically wired to the display (this panel has no MISO/SDO
  // line) - passed to SPIClass purely as a workaround. Passing NC for
  // miso instead hangs forever inside the STM32 core's spi_transfer()
  // (TXP/RXP flag never sets) for SPI4 on this board's custom
  // PeripheralPins variant. PE5 is SPI4-MISO-capable per this variant's
  // own PeripheralPins_WeActMiniH7xx.c, and otherwise unused on this
  // board.
  static constexpr int8_t kPinMisoWorkaround = PE5;

  SPIClass spi_{kPinMosi, kPinMisoWorkaround, kPinSck};

  // xOffset/yOffset/madctl match Adafruit_ST7735's
  // initR(INITR_MINI160x80_PLUGIN) + setRotation(1) - colstart=26/
  // rowstart=1, MADCTL=MY|MV|BGR (0xA8) - the "_PLUGIN" tab, for
  // ribbon/FPC-mounted mini panels like this board's fold-and-tape 0.96"
  // display, not the plain INITR_MINI160x80 tab (colstart=24/rowstart=0,
  // RGB not BGR) tried earlier, which rendered close but not quite right
  // (clean-looking but wrong hues). The _PLUGIN tab also needs INVON
  // instead of INVOFF - see begin()'s comment on that command.
  ST7735DriverHardwareSPI display_{spi_,
                                   kPinCs,
                                   kPinDc,
                                   kPinRst,
                                   /*xOffset=*/1,
                                   /*yOffset=*/26,
                                   /*width=*/160,
                                   /*height=*/80,
                                   /*madctl=*/0xA8};
  I2SPins i2s_{};
  LEDPins led_{};
};

using MiniSTM32H750 = LCDBoardWeActMiniSTM32H750;
using MiniSTM32H7xx = LCDBoardWeActMiniSTM32H750;

}  // namespace tinygpu

#endif  // ARDUINO_ARCH_STM32
