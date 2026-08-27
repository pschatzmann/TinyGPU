#pragma once
/**
 * @file TouchDriverArduino.h
 * @brief Concrete touch-controller drivers (XPT2046, FT6236/FT6206,
 * CST816S, GT911) built on the Arduino SPIClass/TwoWire API - split out
 * of TouchDriver.h so that including the platform-independent TouchDriver
 * base class/calibration/coordinate-mapping code doesn't also pull in
 * Arduino.h. Include this file explicitly to use any of these drivers.
 *
 * Pulls in Arduino.h/SPI.h/Wire.h via TinyGPU/Emulation.h, which falls
 * back to a small platform-native emulation of that surface when no real
 * Arduino core (e.g. arduino-esp32) is present - see that header for
 * what is and isn't covered.
 */

#include "TinyGPU/Emulation.h"
#include "TinyGPU/Input/TouchDriverCommon.h"

namespace tinygpu {

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
