#pragma once
/**
 * @file TouchDriver.h
 * @brief Platform-independent touch driver interface: the Point/
 * CalibrationData/Rotation types, the TouchDriver base class (calibration
 * storage, raw-to-logical coordinate mapping) and nothing else - this file
 * has no dependency on Arduino.h or any bus library, so including it (e.g.
 * transitively via TinyGPU.h) never requires linking against an Arduino
 * core.
 *
 * Concrete controller drivers (XPT2046, FT6236/FT6206, CST816S, GT911),
 * which do need Arduino.h/SPI.h/Wire.h, live in the separate
 * TouchDriverArduino.h - include that explicitly to use one of them.
 */

#include <stdint.h>

namespace tinygpu {

/**
 * @brief Logical display rotation.
 */
enum class Rotation : uint8_t { Deg0 = 0, Deg90 = 1, Deg180 = 2, Deg270 = 3 };

/**
 * @brief Touch point.
 *
 * pressure semantics:
 * - XPT2046: estimated Z/pressure-related value, 0..4095
 * - Capacitive controllers: 255 while touched
 */
struct Point {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t pressure = 0;

  bool operator==(const Point& other) const {
    return x == other.x && y == other.y;
  }

  bool operator!=(const Point& other) const { return !(*this == other); }
};

/**
 * @brief Touch calibration configuration.
 *
 * rawXMin/rawXMax and rawYMin/rawYMax describe the raw controller
 * coordinate ranges.
 *
 * screenWidth/screenHeight describe the unrotated display dimensions.
 *
 * swapXY is applied AFTER raw X/Y calibration. This is important when
 * the X and Y calibration ranges differ.
 */
struct CalibrationData {
  int16_t rawXMin = 300;
  int16_t rawXMax = 3800;
  int16_t rawYMin = 300;
  int16_t rawYMax = 3800;

  uint16_t screenWidth = 240;
  uint16_t screenHeight = 320;

  bool invertX = false;
  bool invertY = false;
  bool swapXY = false;
};

/**
 * @brief Base class for touch controllers.
 */
class TouchDriver {
 public:
  virtual ~TouchDriver() = default;

  /**
   * @brief Initialize the controller.
   *
   * The driver does NOT initialize the SPI/I2C bus itself.
   * The application is responsible for calling Wire.begin(), SPI.begin(),
   * and configuring any board-specific bus pins.
   */
  virtual bool begin() = 0;

  /**
   * @brief Return whether the panel currently appears touched.
   *
   * For controllers without an IRQ pin this may perform a controller read.
   */
  virtual bool isTouched() = 0;

  /**
   * @brief Read the current touch point.
   *
   * @return true if a valid touch point was obtained.
   */
  virtual bool getPoint(Point& outPoint) = 0;

  /**
   * @brief Read a second, simultaneous touch point, for multi-touch
   * gestures (pinch/rotate).
   *
   * Returns false by default. Only override this if the underlying
   * controller can genuinely report two simultaneous touches - none of
   * the drivers built into this library can (XPT2046 is a resistive,
   * inherently single-touch controller; CST816S/FT6236 are single-touch
   * capacitive parts), so pinch/rotate gestures built on top of this will
   * not fire against them.
   */
  virtual bool getSecondPoint(Point& outPoint) { return false; }

  void setRotation(Rotation rotation) { rotation_ = rotation; }

  Rotation getRotation() const { return rotation_; }

  /**
   * @brief Set and validate calibration.
   *
   * @return true if the calibration is valid.
   */
  bool setCalibration(const CalibrationData& cal) {
    if (!validateCalibration(cal)) {
      return false;
    }

    calibration_ = cal;
    hasCalibration_ = true;
    return true;
  }

  const CalibrationData& getCalibration() const { return calibration_; }

  bool hasCalibration() const { return hasCalibration_; }

 protected:
  Rotation rotation_ = Rotation::Deg0;
  CalibrationData calibration_;
  bool hasCalibration_ = false;

  static bool validateCalibration(const CalibrationData& cal) {
    if (cal.rawXMin >= cal.rawXMax) {
      return false;
    }

    if (cal.rawYMin >= cal.rawYMax) {
      return false;
    }

    if (cal.screenWidth == 0 || cal.screenHeight == 0) {
      return false;
    }

    return true;
  }

  /**
   * @brief Clamp an integer to an inclusive range.
   */
  static int32_t clamp32(int32_t value, int32_t minimum, int32_t maximum) {
    if (value < minimum) {
      return minimum;
    }

    if (value > maximum) {
      return maximum;
    }

    return value;
  }

  /**
   * @brief Map an integer from one range to another.
   *
   * Unlike Arduino's map(), this function:
   * - uses 64-bit intermediate arithmetic
   * - explicitly handles invalid ranges
   * - avoids accidental overflow for normal touch-controller ranges
   */
  static int32_t mapRange(int32_t value, int32_t inMin, int32_t inMax,
                          int32_t outMin, int32_t outMax) {
    if (inMin == inMax) {
      return outMin;
    }

    const int64_t numerator = static_cast<int64_t>(value - inMin) *
                              static_cast<int64_t>(outMax - outMin);

    const int64_t denominator = static_cast<int64_t>(inMax - inMin);

    return static_cast<int32_t>(outMin + numerator / denominator);
  }

  /**
   * @brief Convert raw controller coordinates into logical display space.
   *
   * Processing order:
   *
   *   raw coordinates
   *       -> inversion
   *       -> independent X/Y calibration
   *       -> swap XY
   *       -> display rotation
   *
   * This order is important. In particular, swapping raw X/Y before
   * calibration would incorrectly apply the X calibration range to Y
   * and vice versa.
   */
  Point mapCoordinates(int16_t rawX, int16_t rawY,
                       uint16_t pressure = 255) const {
    if (!hasCalibration_) {
      return applyRotation(rawX, rawY, pressure, false);
    }

    int32_t x = rawX;
    int32_t y = rawY;

    /*
     * Inversion is performed in raw/controller space.
     *
     * Clamp first so a slightly out-of-range controller reading cannot
     * produce surprising inverted values.
     */
    x = clamp32(x, calibration_.rawXMin, calibration_.rawXMax);

    y = clamp32(y, calibration_.rawYMin, calibration_.rawYMax);

    if (calibration_.invertX) {
      x = calibration_.rawXMax - (x - calibration_.rawXMin);
    }

    if (calibration_.invertY) {
      y = calibration_.rawYMax - (y - calibration_.rawYMin);
    }

    /*
     * IMPORTANT:
     *
     * Map X against X calibration and Y against Y calibration BEFORE
     * swapping axes.
     */
    int32_t mappedX =
        mapRange(x, calibration_.rawXMin, calibration_.rawXMax, 0,
                 static_cast<int32_t>(calibration_.screenWidth) - 1);

    int32_t mappedY =
        mapRange(y, calibration_.rawYMin, calibration_.rawYMax, 0,
                 static_cast<int32_t>(calibration_.screenHeight) - 1);

    mappedX =
        clamp32(mappedX, 0, static_cast<int32_t>(calibration_.screenWidth) - 1);

    mappedY = clamp32(mappedY, 0,
                      static_cast<int32_t>(calibration_.screenHeight) - 1);

    /*
     * Swap logical axes after calibration.
     */
    if (calibration_.swapXY) {
      const int32_t tmp = mappedX;
      mappedX = mappedY;
      mappedY = tmp;
    }

    return applyRotation(mappedX, mappedY, pressure, true);
  }

 private:
  Point applyRotation(int32_t x, int32_t y, uint16_t pressure,
                      bool calibrated) const {
    if (!calibrated) {
      return {static_cast<int16_t>(x), static_cast<int16_t>(y), pressure};
    }

    const int32_t width = static_cast<int32_t>(calibration_.screenWidth);

    const int32_t height = static_cast<int32_t>(calibration_.screenHeight);

    int32_t finalX = x;
    int32_t finalY = y;

    switch (rotation_) {
      case Rotation::Deg0:
        break;

      case Rotation::Deg90:
        /*
         * Unrotated:
         *   X = [0, width-1]
         *   Y = [0, height-1]
         *
         * Rotated:
         *   X = [0, height-1]
         *   Y = [0, width-1]
         */
        finalX = height - 1 - y;
        finalY = x;
        break;

      case Rotation::Deg180:
        finalX = width - 1 - x;
        finalY = height - 1 - y;
        break;

      case Rotation::Deg270:
        finalX = y;
        finalY = width - 1 - x;
        break;
    }

    return {static_cast<int16_t>(finalX), static_cast<int16_t>(finalY),
            pressure};
  }
};

}  // namespace tinygpu
