#pragma once
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "IFont.h"
#include "SurfaceBase.h"

namespace tinygpu {

/**
 * @brief In-memory 1-bit (monochrome) bitmap surface with 2D drawing and text rendering.
 *
 * SurfaceMonochrome stores pixels in a bit-packed buffer for efficient memory usage.
 * It provides the same drawing and text API as color surfaces, but only supports two pixel values (on/off).
 *
 * This class is ideal for displays or images where only black/white (or on/off) is needed.
 */
class SurfaceMonochrome : public SurfaceBase<bool> {
 public:
  using SurfaceBase<bool>::width_;
  using SurfaceBase<bool>::height_;
  using SurfaceBase<bool>::font_;
  using SurfaceBase<bool>::linePrinter_;
  /// @brief Default constructor. Creates an empty monochrome surface.
  SurfaceMonochrome() = default;

  /// @brief Constructs a monochrome surface with the given width, height, and font.
  /// @param w Surface width in pixels
  /// @param h Surface height in pixels
  /// @param font Reference to the font used for text rendering
  SurfaceMonochrome(size_t w, size_t h, IFont<bool>& font)
      : SurfaceBase<bool>(w, h, font) {}

  /// @brief Sets the pixel at (x, y) to the specified color (on/off).
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @param color Pixel value (true=on, false=off)
  void setPixel(size_t x, size_t y, bool color) override {
    size_t idx = y * width_ + x;
    size_t byte = idx / 8;
    size_t bit = idx % 8;
    if (color)
      buffer[byte] |= (1 << bit);
    else
      buffer[byte] &= ~(1 << bit);
  }

  /// @brief Gets the value of the pixel at (x, y).
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @return Pixel value (true=on, false=off)
  bool getPixel(size_t x, size_t y) const override {
    size_t idx = y * width_ + x;
    size_t byte = idx / 8;
    size_t bit = idx % 8;
    return (buffer[byte] >> bit) & 1;
  }

  /// @brief Resizes the internal bit-packed buffer to the specified width and height.
  /// @param w New width
  /// @param h New height
  void resizeBuffer(size_t w, size_t h) override {
    buffer.resize((w * h + 7) / 8);
  }

  /// @brief Returns a pointer to the raw bit-packed data as bytes.
  /// @return Pointer to the buffer as uint8_t
  const uint8_t* data() const override { return buffer.data(); }

  /// @brief Returns the size of the buffer in bytes.
  /// @return Buffer size in bytes
  size_t size() const override { return buffer.size(); }

 protected:
  std::vector<uint8_t> buffer;
};

/// @brief Alias for a monochrome sprite surface.
using SpriteMonochrome = SurfaceMonochrome;

}  // namespace tinygpu
