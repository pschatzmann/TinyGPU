#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief BGR color stored in 16-bit BGR565 format.
 *
 * The class provides conversion between 8-bit per channel BGR values and the
 * packed 16-bit representation (BGR565) commonly used by embedded displays.
 */
class BGR565 {
 public:
  /// Creates a black BGR565 color.
  BGR565() = default;

  /// Creates a color from 8-bit blue, green, and red components.
  BGR565(uint8_t b, uint8_t g, uint8_t r) { setValue(b, g, r); }

  /// Creates a color from a packed BGR565 value.
  explicit BGR565(uint16_t packed) : value(packed) {}

  /// Sets the color from 8-bit blue, green, and red components.
  void setValue(uint8_t b, uint8_t g, uint8_t r) {
    value = (static_cast<uint16_t>(b & 0xF8) << 8) |
            (static_cast<uint16_t>(g & 0xFC) << 3) |
            (static_cast<uint16_t>(r) >> 3);
  }

  /// Sets the color from a packed BGR565 value.
  void setValue(uint16_t packed) { value = packed; }

  /// Returns the blue component expanded to 8 bits.
  uint8_t getBlue() const {
    const uint8_t blue = (value >> 11) & 0x1F;
    return (blue << 3) | (blue >> 2);
  }

  /// Returns the green component expanded to 8 bits.
  uint8_t getGreen() const {
    const uint8_t green = (value >> 5) & 0x3F;
    return (green << 2) | (green >> 4);
  }

  /// Returns the red component expanded to 8 bits.
  uint8_t getRed() const {
    const uint8_t red = value & 0x1F;
    return (red << 3) | (red >> 2);
  }

  /// Returns the packed BGR565 value.
  uint16_t getValue() const { return value; }

  /// Returns size in bits
  static uint8_t size() { return 16; }

 protected:
  uint16_t value = 0;
};

inline bool operator!=(const tinygpu::BGR565& lhs, const tinygpu::BGR565& rhs) {
  return lhs.getValue() != rhs.getValue();
}
}  // namespace tinygpu
