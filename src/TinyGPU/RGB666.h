#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief RGB color stored in 18-bit RGB666 format.
 *
 * The class provides conversion between 8-bit per channel RGB values and the
 * packed 18-bit representation (3 bytes, 6 bits per channel, upper bits used) commonly used in displays.
 */
class RGB666 {
 public:
  /// Creates a black RGB666 color.
  RGB666() = default;

  /// Creates a color from 8-bit red, green, and blue components.
  RGB666(uint8_t r, uint8_t g, uint8_t b) { setValue(r, g, b); }

  /// Sets the color from 8-bit red, green, and blue components.
  void setValue(uint8_t r, uint8_t g, uint8_t b) {
    value[0] = r & 0xFC; // upper 6 bits
    value[1] = g & 0xFC;
    value[2] = b & 0xFC;
  }

  /// Returns the red component (expanded to 8 bits).
  uint8_t getRed() const { return value[0]; }
  /// Returns the green component (expanded to 8 bits).
  uint8_t getGreen() const { return value[1]; }
  /// Returns the blue component (expanded to 8 bits).
  uint8_t getBlue() const { return value[2]; }

  /// Returns the packed RGB666 value as a pointer to 3 bytes (upper 6 bits used).
  const uint8_t* getValue() const { return value; }

  /// Returns size in bits
  static uint8_t size() { return 18; }

 protected:
  uint8_t value[3] = {0, 0, 0};
};

}  // namespace tinygpu
