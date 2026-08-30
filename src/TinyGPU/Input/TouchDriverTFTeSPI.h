#pragma once
#include <TFT_eSPI.h>  // https://github.com/Bodmer/TFT_eSPI

#include "TinyGPU/Input/TouchDriverCommon.h"

namespace tinygpu {

/**
 * @brief Touch driver that reads through Bodmer's TFT_eSPI
 * (https://github.com/Bodmer/TFT_eSPI) resistive touch support
 * (TFT_eSPI::getTouch()) instead of one of this library's own touch
 * controller drivers (TouchDriverXPT2046, ...) - the companion to
 * DisplayDriverTFTeSPI, for a panel already wired up and calibrated
 * through TFT_eSPI.
 *
 * TFT_eSPI is a separate, optional Arduino library - install it alongside
 * TinyGPU and `#include <TinyGPU/Input/TouchDriverTFTeSPI.h>` explicitly
 * (this header is not pulled in by TinyGPU/Input/TouchDriver.h or
 * TinyGPU.h) to opt in.
 *
 * TFT_eSPI::getTouch() already returns calibrated, rotation-corrected
 * screen-pixel coordinates (matching the display's current
 * TFT_eSPI::setRotation()), so - like TouchDriverSDL - this driver has no
 * use for TouchDriver's own CalibrationData/mapCoordinates() raw-ADC
 * pipeline; leave setCalibration() unset. Calibration itself is entirely
 * TFT_eSPI's job: call tft.setTouch(calData) with data from
 * tft.calibrateTouch(...) (or a hardcoded panel-specific array) before
 * using this driver, the same "driver does not own bus/calibration
 * setup, the application does" division of responsibility
 * TouchDriverArduino.h's drivers use for their own SPI/I2C buses.
 *
 * Like every TouchDriver, isTouched() and getPoint() do not implicitly
 * loop or block. isTouched() is where the actual TFT_eSPI::getTouch()
 * read happens (and its result cached), so - as with this library's other
 * touch drivers - call it exactly once per loop() iteration.
 */
class TouchDriverTFTeSPI : public TouchDriver {
 public:
  /// Wraps an existing TFT_eSPI instance (ownership not taken - construct
  /// and keep it alive for as long as this driver is used, the same way
  /// DisplayDriverTFTeSPI takes its TFT_eSPI& rather than owning one).
  /// `threshold` is forwarded to TFT_eSPI::getTouch() as its pressure
  /// threshold (raw ADC units - see TFT_eSPI's own documentation for what
  /// your panel's controller considers a real touch).
  explicit TouchDriverTFTeSPI(TFT_eSPI& tft, uint16_t threshold = 600)
      : tft_(tft), threshold_(threshold) {}

  /// No-op: TFT_eSPI itself is initialized (and touch-calibrated) by the
  /// application before constructing this driver - see class comment.
  bool begin() override { return true; }

  bool isTouched() override {
    uint16_t x = 0;
    uint16_t y = 0;
    down_ = tft_.getTouch(&x, &y, threshold_) != 0;
    if (down_) {
      lastX_ = static_cast<int16_t>(x);
      lastY_ = static_cast<int16_t>(y);
    }
    return down_;
  }

  bool getPoint(Point& outPoint) override {
    if (!down_) return false;
    outPoint = mapCoordinates(lastX_, lastY_, 255);
    return true;
  }

 private:
  TFT_eSPI& tft_;
  uint16_t threshold_;
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
  bool down_ = false;
};

}  // namespace tinygpu
