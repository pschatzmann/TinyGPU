#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief RGB color stored in 16-bit RGB565 format.
 *
 * The class provides conversion between 8-bit per channel RGB values and the
 * packed 16-bit representation commonly used by embedded displays.
 */
class RGB565 {
 public:

  /// Creates a black RGB565 color.
  RGB565() = default;

  /// Creates a color from 8-bit red, green, and blue components.
  RGB565(uint8_t r, uint8_t g, uint8_t b) { setValue(r, g, b); }

  /// Creates a color from a packed RGB565 value.
  explicit RGB565(uint16_t packed) : value(packed) {}

  /// Creates a color from semantic red/green/blue intensities, regardless
  /// of this class's own constructor argument order. Unlike the (r, g, b)
  /// constructor above - which for other "565-family" classes (e.g.
  /// RBG565, BGR565) matches THEIR class name's letter order, not
  /// necessarily r, g, b - this always means semantic red/green/blue.
  /// Prefer this in generic code that converts between pixel formats.
  static RGB565 fromRGB(uint8_t r, uint8_t g, uint8_t b) {
    return RGB565(r, g, b);
  }

  /// Sets the color from 8-bit red, green, and blue components.
  void setValue(uint8_t r, uint8_t g, uint8_t b) {
      value = (static_cast<uint16_t>(r & 0xF8) << 8) |
              (static_cast<uint16_t>(g & 0xFC) << 3) |
              (static_cast<uint16_t>(b) >> 3);
  }

  /// Sets the color from a packed RGB565 value.
  void setValue(uint16_t packed) { value = packed; }

  /// Returns the red component expanded to 8 bits.
  uint8_t getRed() const {
    const uint8_t red = (value >> 11) & 0x1F;
    return (red << 3) | (red >> 2);
  }

  /// Returns the green component expanded to 8 bits.
  uint8_t getGreen() const {
    const uint8_t green = (value >> 5) & 0x3F;
    return (green << 2) | (green >> 4);
  }

  /// Returns the blue component expanded to 8 bits.
  uint8_t getBlue() const {
    const uint8_t blue = value & 0x1F;
    return (blue << 3) | (blue >> 2);
  }

  /// Returns the packed RGB565 value.
  uint16_t getValue() const { return value; }

  /// Returns size in bits
  static uint8_t size() { return 16; }

 protected:
  uint16_t value = 0;
};

inline bool operator!=(const tinygpu::RGB565& lhs, const tinygpu::RGB565& rhs) {
  return lhs.getValue() != rhs.getValue();
}
}  // namespace tinygpu
