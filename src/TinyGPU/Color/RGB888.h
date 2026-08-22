#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief RGB color stored in 24-bit RGB888 format.
 *
 * The class provides conversion between 8-bit per channel RGB values and the
 * packed 24-bit representation (3 bytes) commonly used in graphics.
 */
class RGB888 {
 public:
  /// Creates a black RGB888 color.
  RGB888() = default;

  /// Creates a color from 8-bit red, green, and blue components.
  RGB888(uint8_t r, uint8_t g, uint8_t b) { setValue(r, g, b); }

  /// Creates a grayscale color from a single 8-bit value (all channels set to v).
  explicit RGB888(uint8_t v) { setValue(v, v, v); }

  /// Sets the color from 8-bit red, green, and blue components.
  void setValue(uint8_t r, uint8_t g, uint8_t b) {
    value[0] = r;
    value[1] = g;
    value[2] = b;
  }

  /// Returns the red component.
  uint8_t getRed() const { return value[0]; }
  /// Returns the green component.
  uint8_t getGreen() const { return value[1]; }
  /// Returns the blue component.
  uint8_t getBlue() const { return value[2]; }

  /// Returns the packed RGB888 value as a pointer to 3 bytes.
  const uint8_t* getValue() const { return value; }

  /// Returns size in bits
  static uint8_t size() { return 24; }

 protected:
  uint8_t value[3] = {0, 0, 0};
};

inline bool operator!=(const tinygpu::RGB888& lhs, const tinygpu::RGB888& rhs) {
  return lhs.getRed() != rhs.getRed() || lhs.getGreen() != rhs.getGreen() || lhs.getBlue() != rhs.getBlue();
}
}  // namespace tinygpu
