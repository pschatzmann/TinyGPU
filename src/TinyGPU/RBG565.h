#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief Color stored in a 16-bit word with an R:5 - B:6 - G:5 bit layout
 * (green and blue swapped relative to RGB565), following the same
 * constructor-order-matches-the-class-name convention as BGR565: pass
 * components in R, B, G order and they're packed into those field
 * positions directly, with no reordering.
 *
 * Some cheap ILI9341-compatible clone controllers don't honor the MADCTL
 * BGR/RGB bit per the datasheet and instead always route the RGB565
 * green-field position to the physical blue subpixel and the blue-field
 * position to the physical green subpixel. On such panels, colors sent as
 * standard RGB565 come out with green and blue swapped (e.g. a green fill
 * shows up blue). Use RBG565 instead and place each color's intensity in
 * the field that actually drives it on that hardware: red as normal, blue
 * as the second argument, green as the third, e.g.
 * RBG565 green(0, 0, 255); RBG565 blue(0, 255, 0);
 */
class RBG565 {
 public:
  /// Creates a black RBG565 color.
  RBG565() = default;

  /// Creates a color from 8-bit red, green, and blue components.
  RBG565(uint8_t r, uint8_t b, uint8_t g) { setValue(r, b, g); }

  /// Creates a color from a packed RBG565 value.
  explicit RBG565(uint16_t packed) : value(packed) {}

  /// Sets the color from 8-bit red, green, and blue components.
  void setValue(uint8_t r, uint8_t b, uint8_t g) {
    value = (static_cast<uint16_t>(r & 0xF8) << 8) |
            (static_cast<uint16_t>(b & 0xFC) << 3) |
            (static_cast<uint16_t>(g) >> 3);
  }

  /// Sets the color from a packed RBG565 value.
  void setValue(uint16_t packed) { value = packed; }

  /// Returns the red component expanded to 8 bits.
  uint8_t getRed() const {
    const uint8_t red = (value >> 11) & 0x1F;
    return (red << 3) | (red >> 2);
  }

  /// Returns the green component expanded to 8 bits.
  uint8_t getGreen() const {
    const uint8_t green = value & 0x1F;
    return (green << 3) | (green >> 2);
  }

  /// Returns the blue component expanded to 8 bits.
  uint8_t getBlue() const {
    const uint8_t blue = (value >> 5) & 0x3F;
    return (blue << 2) | (blue >> 4);
  }

  /// Returns the packed RBG565 value.
  uint16_t getValue() const { return value; }

  /// Returns size in bits
  static uint8_t size() { return 16; }

 protected:
  uint16_t value = 0;
};

inline bool operator!=(const tinygpu::RBG565& lhs, const tinygpu::RBG565& rhs) {
  return lhs.getValue() != rhs.getValue();
}
}  // namespace tinygpu
