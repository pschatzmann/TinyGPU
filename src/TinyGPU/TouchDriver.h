#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <stdbool.h>
#include <stdint.h>

namespace tinygpu {

enum class Rotation : uint8_t { Deg0 = 0, Deg90 = 1, Deg180 = 2, Deg270 = 3 };

struct Point {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t pressure = 0;

  bool operator==(const Point& other) const {
    return x == other.x && y == other.y;
  }
};

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

class TouchDriver {
 public:
  virtual ~TouchDriver() = default;

  virtual bool begin() = 0;
  virtual bool isTouched() = 0;
  virtual bool getPoint(Point& outPoint) = 0;

  void setRotation(Rotation rotation) { rotation_ = rotation; }
  Rotation getRotation() const { return rotation_; }

  void setCalibration(const CalibrationData& cal) {
    calibration_ = cal;
    hasCalibration_ = true;
  }

 protected:
  Rotation rotation_ = Rotation::Deg0;
  CalibrationData calibration_;
  bool hasCalibration_ = false;

  // Helper function to map raw coordinates to screen space based on calibration
  // and rotation
  Point mapCoordinates(int16_t rawX, int16_t rawY,
                       uint16_t pressure = 255) const {
    if (!hasCalibration_) {
      return {rawX, rawY, pressure};
    }

    int16_t x = rawX;
    int16_t y = rawY;

    // Apply raw inversion
    if (calibration_.invertX)
      x = calibration_.rawXMax - (x - calibration_.rawXMin);
    if (calibration_.invertY)
      y = calibration_.rawYMax - (y - calibration_.rawYMin);

    // Swap axes if requested
    if (calibration_.swapXY) {
      int16_t tmp = x;
      x = y;
      y = tmp;
    }

    // Map raw values to screen pixel bounds
    int32_t mappedX = map(x, calibration_.rawXMin, calibration_.rawXMax, 0,
                          calibration_.screenWidth);
    int32_t mappedY = map(y, calibration_.rawYMin, calibration_.rawYMax, 0,
                          calibration_.screenHeight);

    // Constrain
    mappedX = constrain(mappedX, 0, (int32_t)calibration_.screenWidth - 1);
    mappedY = constrain(mappedY, 0, (int32_t)calibration_.screenHeight - 1);

    // Handle logical display rotation
    int16_t finalX = mappedX;
    int16_t finalY = mappedY;

    switch (rotation_) {
      case Rotation::Deg90:
        finalX = calibration_.screenHeight - 1 - mappedY;
        finalY = mappedX;
        break;
      case Rotation::Deg180:
        finalX = calibration_.screenWidth - 1 - mappedX;
        finalY = calibration_.screenHeight - 1 - mappedY;
        break;
      case Rotation::Deg270:
        finalX = mappedY;
        finalY = calibration_.screenWidth - 1 - mappedX;
        break;
      case Rotation::Deg0:
      default:
        break;
    }

    return {finalX, finalY, pressure};
  }
};

/**
 * @brief Driver for XPT2046 touch controller (resistive touch).
 *
 * Handles SPI communication and touch point reading for XPT2046.
 */

class TouchDriverXPT2046 : public TouchDriver {
 public:
  TouchDriverXPT2046(SPIClass& spi, int8_t csPin, int8_t irqPin = -1,
                     uint16_t zThreshold = 400)
      : spi_(spi), csPin_(csPin), irqPin_(irqPin), zThreshold_(zThreshold) {}

  bool begin() override {
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
    Point dummy;
    return getPoint(dummy);
  }

  bool getPoint(Point& outPoint) override {
    spi_.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(csPin_, LOW);

    // Read Z1 & Z2 to calculate pressure
    uint16_t z1 = transfer12(0xB1);
    uint16_t z2 = transfer12(0xC1);
    uint16_t z = z1 + 4095 - z2;

    if (z < zThreshold_) {
      digitalWrite(csPin_, HIGH);
      spi_.endTransaction();
      return false;
    }

    // Read X & Y
    transfer12(0x90);  // Dummy read to settle
    uint16_t rawX = transfer12(0x90);
    uint16_t rawY = transfer12(0xD0);

    digitalWrite(csPin_, HIGH);
    spi_.endTransaction();

    outPoint = mapCoordinates(rawX, rawY, z);
    return true;
  }

 private:
  SPIClass& spi_;
  int8_t csPin_;
  int8_t irqPin_;
  uint16_t zThreshold_;

  uint16_t transfer12(uint8_t cmd) {
    spi_.transfer(cmd);
    uint8_t high = spi_.transfer(0x00);
    uint8_t low = spi_.transfer(0x00);
    return ((high << 8) | low) >> 3;
  }
};

/**
 * @brief Driver for FT6236/FT6336 touch controller (capacitive touch).
 * using I2C communication to read touch points.
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

    wire_.beginTransmission(I2C_ADDR);
    return (wire_.endTransmission() == 0);
  }

  bool isTouched() override {
    if (irqPin_ >= 0 && digitalRead(irqPin_) == HIGH) {
      return false;
    }
    wire_.beginTransmission(I2C_ADDR);
    wire_.write(0x02);  // TD_STATUS register
    if (wire_.endTransmission() != 0) return false;

    wire_.requestFrom(I2C_ADDR, (uint8_t)1);
    if (!wire_.available()) return false;

    uint8_t touches = wire_.read() & 0x0F;
    return (touches > 0 && touches <= 2);
  }

  bool getPoint(Point& outPoint) override {
    if (!isTouched()) return false;

    wire_.beginTransmission(I2C_ADDR);
    wire_.write(0x03);  // P1_XH register
    if (wire_.endTransmission() != 0) return false;

    wire_.requestFrom(I2C_ADDR, (uint8_t)4);
    if (wire_.available() < 4) return false;

    uint8_t xh = wire_.read();
    uint8_t xl = wire_.read();
    uint8_t yh = wire_.read();
    uint8_t yl = wire_.read();

    uint16_t rawX = ((xh & 0x0F) << 8) | xl;
    uint16_t rawY = ((yh & 0x0F) << 8) | yl;

    outPoint = mapCoordinates(rawX, rawY, 255);
    return true;
  }

 private:
  TwoWire& wire_;
  int8_t rstPin_;
  int8_t irqPin_;
};

/**
 * @brief Driver for CST816S touch controller (capacitive touch).
 *
 * Handles I2C communication and touch point reading for CST816S.
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

    wire_.beginTransmission(I2C_ADDR);
    return (wire_.endTransmission() == 0);
  }

  bool isTouched() override {
    if (irqPin_ >= 0 && digitalRead(irqPin_) == HIGH) {
      return false;
    }

    wire_.beginTransmission(I2C_ADDR);
    wire_.write(0x02);  // Finger num register
    if (wire_.endTransmission() != 0) return false;

    wire_.requestFrom(I2C_ADDR, (uint8_t)1);
    if (!wire_.available()) return false;

    return (wire_.read() > 0);
  }

  bool getPoint(Point& outPoint) override {
    wire_.beginTransmission(I2C_ADDR);
    wire_.write(0x01);  // Read gesture & coordinate registers
    if (wire_.endTransmission() != 0) return false;

    wire_.requestFrom(I2C_ADDR, (uint8_t)6);
    if (wire_.available() < 6) return false;

    wire_.read();  // Gesture
    uint8_t fingerNum = wire_.read();
    uint8_t xh = wire_.read();
    uint8_t xl = wire_.read();
    uint8_t yh = wire_.read();
    uint8_t yl = wire_.read();

    if (fingerNum == 0) return false;

    uint16_t rawX = ((xh & 0x0F) << 8) | xl;
    uint16_t rawY = ((yh & 0x0F) << 8) | yl;

    outPoint = mapCoordinates(rawX, rawY, 255);
    return true;
  }

 private:
  TwoWire& wire_;
  int8_t rstPin_;
  int8_t irqPin_;
};

}  // namespace tinygpu