#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
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

/**
 * @brief XPT2046 resistive touch controller.
 *
 * Uses SPI.
 *
 * XPT2046 command mapping:
 *   0x90 = Y position
 *   0xD0 = X position
 *   0xB1 = Z1
 *   0xC1 = Z2
 */
class TouchDriverXPT2046 : public TouchDriver {
 public:
  TouchDriverXPT2046(SPIClass& spi, int8_t csPin, int8_t irqPin = -1,
                     uint16_t zThreshold = 400)
      : spi_(spi), csPin_(csPin), irqPin_(irqPin), zThreshold_(zThreshold) {}

  bool begin() override {
    if (csPin_ < 0) {
      return false;
    }

    pinMode(csPin_, OUTPUT);
    digitalWrite(csPin_, HIGH);

    if (irqPin_ >= 0) {
      pinMode(irqPin_, INPUT_PULLUP);
    }

    return true;
  }

  bool isTouched() override {
    if (irqPin_ >= 0) {
      return digitalRead(irqPin_) == LOW;
    }

    /*
     * Without PENIRQ we need to perform an actual controller measurement.
     */
    Point point;
    return getPoint(point);
  }

  bool getPoint(Point& outPoint) override {
    spi_.beginTransaction(SPISettings(kSpiClock, MSBFIRST, SPI_MODE0));

    digitalWrite(csPin_, LOW);

    /*
     * Read Z1 and Z2 first to determine whether the panel is pressed.
     */
    const uint16_t z1 = transfer12(0xB1);
    const uint16_t z2 = transfer12(0xC1);

    /*
     * XPT2046's commonly used touch-presence metric:
     *
     *     Z = Z1 + 4095 - Z2
     *
     * This is useful for thresholding but is NOT a calibrated physical
     * pressure value in ohms or grams.
     */
    const int32_t z =
        static_cast<int32_t>(z1) + 4095 - static_cast<int32_t>(z2);

    if (z < static_cast<int32_t>(zThreshold_)) {
      endTransaction();
      return false;
    }

    /*
     * IMPORTANT:
     *
     * 0xD0 = X
     * 0x90 = Y
     *
     * The previous implementation had these reversed.
     *
     * Discard the first conversion for each axis. This improves stability
     * on some XPT2046/panel combinations.
     */
    (void)transfer12(0xD0);
    const uint16_t rawX = transfer12(0xD0);

    (void)transfer12(0x90);
    const uint16_t rawY = transfer12(0x90);

    endTransaction();

    /*
     * Clamp Z to the representable 12-bit range before exposing it.
     */
    const uint16_t pressure = static_cast<uint16_t>(clamp32(z, 0, 4095));

    outPoint = mapCoordinates(static_cast<int16_t>(rawX),
                              static_cast<int16_t>(rawY), pressure);

    return true;
  }

  void setZThreshold(uint16_t threshold) { zThreshold_ = threshold; }

  uint16_t getZThreshold() const { return zThreshold_; }

 private:
  static constexpr uint32_t kSpiClock = 2000000UL;

  SPIClass& spi_;
  int8_t csPin_;
  int8_t irqPin_;
  uint16_t zThreshold_;

  uint16_t transfer12(uint8_t command) {
    spi_.transfer(command);

    const uint8_t high = spi_.transfer(0x00);

    const uint8_t low = spi_.transfer(0x00);

    return static_cast<uint16_t>(
        ((static_cast<uint16_t>(high) << 8) | static_cast<uint16_t>(low)) >> 3);
  }

  void endTransaction() {
    digitalWrite(csPin_, HIGH);
    spi_.endTransaction();
  }
};

/**
 * @brief FT6236 / FT6336 capacitive touch controller.
 *
 * I2C address is normally 0x38.
 *
 * Register layout used:
 *
 *   0x02 TD_STATUS
 *   0x03 P1_XH
 *   0x04 P1_XL
 *   0x05 P1_YH
 *   0x06 P1_YL
 */
class TouchDriverFT6236 : public TouchDriver {
 public:
  static constexpr uint8_t I2C_ADDR = 0x38;

  TouchDriverFT6236(TwoWire& wire = Wire, int8_t rstPin = -1,
                    int8_t irqPin = -1)
      : wire_(wire), rstPin_(rstPin), irqPin_(irqPin) {}

  bool begin() override {
    if (rstPin_ >= 0) {
      pinMode(rstPin_, OUTPUT);

      digitalWrite(rstPin_, LOW);
      delay(10);

      digitalWrite(rstPin_, HIGH);
      delay(300);
    }

    if (irqPin_ >= 0) {
      pinMode(irqPin_, INPUT_PULLUP);
    }

    /*
     * The caller owns Wire.begin().
     *
     * We only verify that the device responds.
     */
    wire_.beginTransmission(I2C_ADDR);

    return wire_.endTransmission() == 0;
  }

  bool isTouched() override {
    if (irqPin_ >= 0 && digitalRead(irqPin_) == HIGH) {
      return false;
    }

    uint8_t buffer[5];

    if (!readRegisters(0x02, buffer, sizeof(buffer))) {
      return false;
    }

    const uint8_t touches = buffer[0] & 0x0F;

    return touches > 0;
  }

  bool getPoint(Point& outPoint) override {
    if (irqPin_ >= 0 && digitalRead(irqPin_) == HIGH) {
      return false;
    }

    /*
     * Read status + P1 coordinates in one transaction:
     *
     *   0x02 TD_STATUS
     *   0x03 P1_XH
     *   0x04 P1_XL
     *   0x05 P1_YH
     *   0x06 P1_YL
     */
    uint8_t buffer[5];

    if (!readRegisters(0x02, buffer, sizeof(buffer))) {
      return false;
    }

    const uint8_t touches = buffer[0] & 0x0F;

    if (touches == 0) {
      return false;
    }

    const uint16_t rawX =
        (static_cast<uint16_t>(buffer[1] & 0x0F) << 8) | buffer[2];

    const uint16_t rawY =
        (static_cast<uint16_t>(buffer[3] & 0x0F) << 8) | buffer[4];

    /*
     * Capacitive controller does not expose resistive pressure in the
     * same sense as XPT2046. 255 means "touched".
     */
    outPoint = mapCoordinates(static_cast<int16_t>(rawX),
                              static_cast<int16_t>(rawY), 255);

    return true;
  }

 private:
  TwoWire& wire_;
  int8_t rstPin_;
  int8_t irqPin_;

  bool readRegisters(uint8_t startRegister, uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length == 0) {
      return false;
    }

    wire_.beginTransmission(I2C_ADDR);
    wire_.write(startRegister);

    if (wire_.endTransmission(false) != 0) {
      return false;
    }

    const size_t received =
        wire_.requestFrom(static_cast<int>(I2C_ADDR), static_cast<int>(length));

    if (received != length) {
      /*
       * Drain anything the implementation may have left in the RX buffer.
       */
      while (wire_.available()) {
        (void)wire_.read();
      }

      return false;
    }

    for (size_t i = 0; i < length; ++i) {
      if (!wire_.available()) {
        return false;
      }

      buffer[i] = static_cast<uint8_t>(wire_.read());
    }

    return true;
  }
};


/// The FT6206 is a drop-in replacement for the FT6236, so we can alias it here.
using TouchDriverFT6206 = TouchDriverFT6236;


/**
 * @brief CST816S capacitive touch controller.
 *
 * I2C address:
 *   0x15
 *
 * Register layout:
 *
 *   0x01 GestureID
 *   0x02 FingerNum
 *   0x03 XposH
 *   0x04 XposL
 *   0x05 YposH
 *   0x06 YposL
 */
class TouchDriverCST816S : public TouchDriver {
 public:
  static constexpr uint8_t I2C_ADDR = 0x15;

  TouchDriverCST816S(TwoWire& wire = Wire, int8_t rstPin = -1,
                     int8_t irqPin = -1)
      : wire_(wire), rstPin_(rstPin), irqPin_(irqPin) {}

  bool begin() override {
    if (rstPin_ >= 0) {
      pinMode(rstPin_, OUTPUT);

      digitalWrite(rstPin_, LOW);
      delay(20);

      digitalWrite(rstPin_, HIGH);
      delay(50);
    }

    if (irqPin_ >= 0) {
      pinMode(irqPin_, INPUT_PULLUP);
    }

    /*
     * The application owns Wire.begin().
     */
    wire_.beginTransmission(I2C_ADDR);

    return wire_.endTransmission() == 0;
  }

  bool isTouched() override {
    if (irqPin_ >= 0 && digitalRead(irqPin_) == HIGH) {
      return false;
    }

    uint8_t fingerNum = 0;

    if (!readRegisters(0x02, &fingerNum, 1)) {
      return false;
    }

    return fingerNum != 0;
  }

  bool getPoint(Point& outPoint) override {
    if (irqPin_ >= 0 && digitalRead(irqPin_) == HIGH) {
      return false;
    }

    /*
     * Read all relevant touch information in one transaction:
     *
     *   0x01 GestureID
     *   0x02 FingerNum
     *   0x03 XposH
     *   0x04 XposL
     *   0x05 YposH
     *   0x06 YposL
     */
    uint8_t buffer[6];

    if (!readRegisters(0x01, buffer, sizeof(buffer))) {
      return false;
    }

    const uint8_t fingerNum = buffer[1];

    if (fingerNum == 0) {
      return false;
    }

    const uint16_t rawX =
        (static_cast<uint16_t>(buffer[2] & 0x0F) << 8) | buffer[3];

    const uint16_t rawY =
        (static_cast<uint16_t>(buffer[4] & 0x0F) << 8) | buffer[5];

    outPoint = mapCoordinates(static_cast<int16_t>(rawX),
                              static_cast<int16_t>(rawY), 255);

    return true;
  }

 private:
  TwoWire& wire_;
  int8_t rstPin_;
  int8_t irqPin_;

  bool readRegisters(uint8_t startRegister, uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length == 0) {
      return false;
    }

    wire_.beginTransmission(I2C_ADDR);
    wire_.write(startRegister);

    /*
     * Repeated-start is preferable because it keeps the register address
     * transaction together with the subsequent read.
     */
    if (wire_.endTransmission(false) != 0) {
      return false;
    }

    const size_t received =
        wire_.requestFrom(static_cast<int>(I2C_ADDR), static_cast<int>(length));

    if (received != length) {
      while (wire_.available()) {
        (void)wire_.read();
      }

      return false;
    }

    for (size_t i = 0; i < length; ++i) {
      if (!wire_.available()) {
        return false;
      }

      buffer[i] = static_cast<uint8_t>(wire_.read());
    }

    return true;
  }
};

/**
 * @brief GT911 capacitive touch controller.
 *
 * I2C address is normally 0x5D (alt 0x14, selected via the INT pin's level
 * while RST is pulsed - see begin()). Unlike FT6236/CST816S, GT911's
 * register addresses are 16-bit (MSB first), and its X/Y bytes are
 * little-endian (low byte first) rather than the big-endian nibble-packed
 * layout FT6236 uses.
 *
 * Register layout used:
 *
 *   0x8100  CONFIG_FRESH: write 1 after reset to make the chip start its
 *           sensing engine. Without this, the chip stays fully alive and
 *           correctly configured but never scans - status sits at 0x80
 *           ("config loaded, idle") forever.
 *   0x80FF  config checksum: round-tripped (read then written back
 *           unchanged) alongside the CONFIG_FRESH write.
 *   0x814E  status: bit7 = buffer ready, bits[3:0] = touch point count.
 *           Must be cleared (write 0x00) unconditionally on every poll,
 *           not only when a touch was found - clearing it only after a
 *           detected touch is a deadlock, since a stale, unacknowledged
 *           flag prevents the chip from ever reporting a new one.
 *   0x814F  point 0 data (7 bytes: track_id, x_lo, x_hi, y_lo, y_hi,
 *           size_lo, size_hi); point N starts at 0x814F + N*7.
 *
 * Because status must be cleared unconditionally exactly once per poll
 * cycle, isTouched() does the chip's actual read/clear I/O and caches
 * whatever point data was present; getPoint()/getSecondPoint() just
 * return the cached result rather than re-querying the chip. Call
 * isTouched() once per loop iteration (the same usage pattern as this
 * file's other touch drivers) - calling it more than once between
 * getPoint() calls will consume/clear a pending touch before getPoint()
 * sees it.
 *
 * GT911 genuinely reports up to 5 simultaneous touches (unlike the
 * single-touch FT6236/CST816S/XPT2046 parts also in this file), so
 * getSecondPoint() is a real second contact, not a stub.
 */
class TouchDriverGT911 : public TouchDriver {
 public:
  static constexpr uint8_t I2C_ADDR_1 = 0x5D;
  static constexpr uint8_t I2C_ADDR_2 = 0x14;

  TouchDriverGT911(TwoWire& wire = Wire, int8_t rstPin = -1,
                   int8_t irqPin = -1, uint8_t i2cAddr = I2C_ADDR_1)
      : wire_(wire), rstPin_(rstPin), irqPin_(irqPin), addr_(i2cAddr) {}

  bool begin() override {
    if (rstPin_ >= 0) {
      /*
       * Address-select reset sequence (GT911 datasheet): INT is held at
       * the level matching the desired I2C address while RST is pulsed,
       * then released to become the (unused, polled) touch IRQ line.
       */
      pinMode(rstPin_, OUTPUT);

      if (irqPin_ >= 0) {
        pinMode(irqPin_, OUTPUT);
        digitalWrite(irqPin_, addr_ == I2C_ADDR_1 ? LOW : HIGH);
      }

      digitalWrite(rstPin_, LOW);
      delay(10);

      if (irqPin_ >= 0) {
        digitalWrite(irqPin_, addr_ == I2C_ADDR_1 ? LOW : HIGH);
      }
      delay(1);

      digitalWrite(rstPin_, HIGH);
      delay(5);

      if (irqPin_ >= 0) {
        digitalWrite(irqPin_, LOW);
      }
      delay(50);
    }

    if (irqPin_ >= 0) {
      pinMode(irqPin_, INPUT_PULLUP);
    }

    delay(50);  // GT911 needs time after reset before it will ACK on I2C

    wire_.beginTransmission(addr_);
    if (wire_.endTransmission() != 0) {
      return false;
    }

    /* Activate the sensing engine - see class comment. */
    uint8_t checksum = 0;
    readRegister(kRegConfigChecksum, checksum);
    writeRegister(kRegConfigChecksum, checksum);
    writeRegister(kRegConfigFresh, 0x01);
    delay(10);

    return true;
  }

  bool isTouched() override {
    uint8_t status = 0;
    readRegister(kRegStatus, status);
    const bool bufferReady = (status & 0x80) != 0;
    cachedTouches_ = status & 0x0F;

    if (bufferReady && cachedTouches_ > 0) {
      const uint8_t pointsToRead =
          cachedTouches_ > 2 ? 2 : cachedTouches_;  // this driver only caches 2

      for (uint8_t i = 0; i < pointsToRead; ++i) {
        uint8_t buf[7];
        if (readRegisters(kRegPoint0 + i * 7, buf, sizeof(buf))) {
          const uint16_t rawX = (static_cast<uint16_t>(buf[2]) << 8) | buf[1];
          const uint16_t rawY = (static_cast<uint16_t>(buf[4]) << 8) | buf[3];
          cachedPoints_[i] = mapCoordinates(static_cast<int16_t>(rawX),
                                            static_cast<int16_t>(rawY), 255);
        }
      }
    }

    writeRegister(kRegStatus, 0x00);  // unconditional - see class comment
    return cachedTouches_ > 0;
  }

  bool getPoint(Point& outPoint) override {
    if (cachedTouches_ == 0) return false;
    outPoint = cachedPoints_[0];
    return true;
  }

  bool getSecondPoint(Point& outPoint) override {
    if (cachedTouches_ < 2) return false;
    outPoint = cachedPoints_[1];
    return true;
  }

 private:
  static constexpr uint16_t kRegConfigChecksum = 0x80FF;
  static constexpr uint16_t kRegConfigFresh = 0x8100;
  static constexpr uint16_t kRegStatus = 0x814E;
  static constexpr uint16_t kRegPoint0 = 0x814F;

  TwoWire& wire_;
  int8_t rstPin_;
  int8_t irqPin_;
  uint8_t addr_;
  uint8_t cachedTouches_ = 0;
  Point cachedPoints_[2];

  bool readRegister(uint16_t reg, uint8_t& value) {
    return readRegisters(reg, &value, 1);
  }

  bool readRegisters(uint16_t startRegister, uint8_t* buffer, size_t length) {
    if (buffer == nullptr || length == 0) {
      return false;
    }

    wire_.beginTransmission(addr_);
    wire_.write(static_cast<uint8_t>(startRegister >> 8));
    wire_.write(static_cast<uint8_t>(startRegister & 0xFF));

    if (wire_.endTransmission(false) != 0) {
      return false;
    }

    const size_t received =
        wire_.requestFrom(static_cast<int>(addr_), static_cast<int>(length));

    if (received != length) {
      while (wire_.available()) {
        (void)wire_.read();
      }
      return false;
    }

    for (size_t i = 0; i < length; ++i) {
      if (!wire_.available()) {
        return false;
      }
      buffer[i] = static_cast<uint8_t>(wire_.read());
    }

    return true;
  }

  void writeRegister(uint16_t reg, uint8_t value) {
    wire_.beginTransmission(addr_);
    wire_.write(static_cast<uint8_t>(reg >> 8));
    wire_.write(static_cast<uint8_t>(reg & 0xFF));
    wire_.write(value);
    wire_.endTransmission();
  }
};

}  // namespace tinygpu