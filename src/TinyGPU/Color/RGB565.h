#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief RGB color stored in 16-bit RGB565 format, byte-swapped from the
 * conventional bit layout.
 *
 * The class provides conversion between 8-bit per channel RGB values and the
 * packed 16-bit representation commonly used by embedded displays.
 *
 * Storage note: `value` holds the two bytes of the conventional RGB565
 * packing (R:5 G:6 B:5, red in the high bits) swapped - i.e. exactly the
 * byte order every SPI/QSPI panel driver in this codebase (DisplayDriverSPI,
 * DisplayDriverQSPI) needs on the wire. That's the overwhelmingly common
 * consumer, so getValue()/writeData() need no per-pixel swap. Anything that
 * instead needs the conventional (native) bit layout - LVGL's own pixel
 * buffer, SDL's SDL_PIXELFORMAT_RGB565 texture upload, or a file format like
 * AVI/BMP that declares standard RGB565 bitmasks - must go through
 * getValueSwapped() (or construct via the swapped packed value) to undo it.
 */
class RGB565 {
 public:

  /// Creates a black RGB565 color.
  RGB565() = default;

  /// Creates a color from 8-bit red, green, and blue components.
  RGB565(uint8_t r, uint8_t g, uint8_t b) { setValue(r, g, b); }

  /// Creates a color from a packed value already in this class's stored
  /// (byte-swapped) order - i.e. one that round-trips through getValue().
  /// To decode a packed value in the conventional (native) bit layout
  /// instead - e.g. a raw pixel from LVGL's own buffer - byte-swap it
  /// first: RGB565(swapBytes(nativePacked)).
  explicit RGB565(uint16_t packed) : value(packed) {}

  /// Creates a color from semantic red/green/blue intensities, regardless
  /// of this class's own constructor argument order. Prefer this in
  /// generic code that converts between pixel formats.
  static RGB565 fromRGB(uint8_t r, uint8_t g, uint8_t b) {
    return RGB565(r, g, b);
  }

  /// Byte-swaps a 16-bit value; used to convert between this class's stored
  /// (wire) order and the conventional (native) RGB565 bit layout.
  static uint16_t swapBytes(uint16_t v) {
    return static_cast<uint16_t>((v << 8) | (v >> 8));
  }

  /// Sets the color from 8-bit red, green, and blue components.
  void setValue(uint8_t r, uint8_t g, uint8_t b) {
      const uint16_t packed = (static_cast<uint16_t>(r & 0xF8) << 8) |
                               (static_cast<uint16_t>(g & 0xFC) << 3) |
                               (static_cast<uint16_t>(b) >> 3);
      value = swapBytes(packed);
  }

  /// Sets the color from a packed value already in this class's stored
  /// (byte-swapped) order - see the constructor note above.
  void setValue(uint16_t packed) { value = packed; }

  /// Returns the red component expanded to 8 bits.
  uint8_t getRed() const {
    const uint8_t red = (swapBytes(value) >> 11) & 0x1F;
    return (red << 3) | (red >> 2);
  }

  /// Returns the green component expanded to 8 bits.
  uint8_t getGreen() const {
    const uint8_t green = (swapBytes(value) >> 5) & 0x3F;
    return (green << 2) | (green >> 4);
  }

  /// Returns the blue component expanded to 8 bits.
  uint8_t getBlue() const {
    const uint8_t blue = swapBytes(value) & 0x1F;
    return (blue << 3) | (blue >> 2);
  }

  /// Returns the packed value in this class's stored (byte-swapped) order -
  /// i.e. wire-ready for DisplayDriverSPI/DisplayDriverQSPI with no further
  /// swap needed.
  uint16_t getValue() const { return value; }

  /// Returns the packed value in the conventional (native) RGB565 bit
  /// layout (R:5 G:6 B:5, red in the high bits) - the order LVGL, SDL, and
  /// standard RGB565 file-format bitmasks (e.g. AVI/BMP) expect.
  uint16_t getValueSwapped() const { return swapBytes(value); }

  /// Returns size in bits
  static uint8_t size() { return 16; }

 protected:
  uint16_t value = 0;
};

inline bool operator!=(const tinygpu::RGB565& lhs, const tinygpu::RGB565& rhs) {
  return lhs.getValue() != rhs.getValue();
}
}  // namespace tinygpu
