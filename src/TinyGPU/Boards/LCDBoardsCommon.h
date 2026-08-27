#pragma once
/**
 * @file LCDBoards.h
 * @brief Platform-independent interface for a board that bundles a
 * display, an optional touch controller, an I2S pin assignment, and an
 * LED pin assignment behind one begin() call.
 *
 * This file only defines the shared interface (I2SPins, LEDPins,
 * LCDBoard). Concrete board classes with actual pin numbers live in
 * platform-specific files, e.g. LCDBoardsESP32.h for ESP32(-S3) boards
 * from the arduino-audio-tools "Audio Boards" wiki:
 * https://github.com/pschatzmann/arduino-audio-tools/wiki/Audio-Boards
 */
#include "../../TinyGPU.h"
#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Input/TouchDriver.h"

namespace tinygpu {

/// I2S pin assignment for a board's audio codec/amplifier. Not used by
/// TinyGPU itself - hand these to the audio library of choice (e.g.
/// audio_tools::I2SConfig's pin_mck/pin_bck/pin_ws/pin_data/pin_data_rx).
/// -1 means "not present on this board".
///
/// paEnable/paEnableActiveLow describe a separate GPIO some boards use to
/// gate the speaker amplifier (not part of the I2S bus itself, but bundled
/// here since it's only ever needed alongside these pins).
struct I2SPins {
  int8_t mclk = -1;
  int8_t bck = -1;
  int8_t ws = -1;
  int8_t dataOut = -1;
  int8_t dataIn = -1;
  int8_t paEnable = -1;
  bool paEnableActiveLow = false;

  /// True if this board has a usable I2S bus (bck/ws plus at least one of
  /// dataOut/dataIn) wired up.
  bool isValid() const {
    return bck != -1 && ws != -1 && (dataOut != -1 || dataIn != -1);
  }
};

/// LED pin assignment for a board's status LED, if any. Boards in this
/// file use one of two shapes: a single-wire addressable LED (WS2812 and
/// similar - set `single`) or a discrete 3-pin RGB LED (set `r`/`g`/`b`
/// and `activeLow`). -1 means "no pin of that kind on this board" - a
/// board with neither leaves every field at -1.
struct LEDPins {
  int8_t single = -1;
  int8_t r = -1;
  int8_t g = -1;
  int8_t b = -1;
  bool activeLow = false;
};

/// Common interface for a board that bundles a display, an optional touch
/// controller, an I2S pin assignment, and an LED pin assignment behind one
/// begin() call. All boards implementing this use RGB565 panels, so
/// display() is pinned to that color type here (each concrete board still
/// returns its own driver subclass via a covariant return type).
class LCDBoard {
 public:
  virtual ~LCDBoard() = default;

  /// Brings up the board's backlight/bus/display/touch. Returns false if
  /// the display or touch begin() fails.
  virtual bool begin() = 0;

  /// Panel width in pixels.
  virtual size_t width() const = 0;
  /// Panel height in pixels.
  virtual size_t height() const = 0;

  /// The board's display driver.
  virtual DisplayDriver<RGB565>& display() = 0;
  /// The board's touch controller, or nullptr if none is wired up.
  virtual TouchDriver* touch() = 0;
  /// The board's I2S/PA pin assignment.
  virtual const I2SPins& i2s() const = 0;
  /// The board's LED pin assignment.
  virtual const LEDPins& led() const = 0;
  /// The board's backlight GPIO pin, or -1 if unknown/not applicable.
  /// begin() already drives it digitally HIGH; this accessor exists so
  /// callers can reconfigure the same pin for PWM brightness control
  /// (e.g. via ledcAttach/ledcWrite) after begin().
  virtual int8_t backlightPin() const { return -1; }

  /// Copies this board's I2S pins into an audio_tools-style I2SConfig
  /// (any type exposing pin_bck/pin_ws/pin_data/pin_data_rx).
  template <typename I2SConfig>
  void setI2SPins(I2SConfig& cfg) {
    const I2SPins& pins = i2s();
    cfg.pin_mck = pins.mclk;
    cfg.pin_bck = pins.bck;
    cfg.pin_ws = pins.ws;
    cfg.pin_data = pins.dataOut;
    cfg.pin_data_rx = pins.dataIn;
  }
};

}  // namespace tinygpu
