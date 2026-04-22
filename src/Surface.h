
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cstddef>
#include <vector>

#include "BGR565.h"
#include "BitmapFont.h"
#include "IFont.h"
#include "ISurface.h"
#include "LinePrinter.h"
#include "RGB565.h"
#include "RGB666.h"
#include "RGB888.h"
#include "SurfaceBase.h"
#include "TinyGPUConfig.h"

namespace tinygpu {

/**
 * @brief In-memory bitmap surface with basic 2D drawing and text rendering.
 *
 * @tparam RGB_T The pixel color type. Can be RGB565, RGB666, RGB888, etc.
 *
 * The class stores pixel data in a contiguous buffer and provides helpers for
 * drawing lines, rectangles, circles, sprites, and UTF-8 text.
 *
 * Example usage:
 *   Surface<RGB565> surf565(240, 320);
 *   Surface<RGB666> surf666(240, 320);
 *   Surface<RGB888> surf888(240, 320);
 */

template <typename RGB_T = RGB565>
class Surface : public SurfaceBase<RGB_T> {
  static_assert(!std::is_same<RGB_T, bool>::value,
                "Use SurfaceMonochrome for monochrome surfaces");

 public:
  using SurfaceBase<RGB_T>::width_;
  using SurfaceBase<RGB_T>::height_;
  using SurfaceBase<RGB_T>::font_;
  using SurfaceBase<RGB_T>::linePrinter_;
  /// @brief Default constructor. Creates an empty surface.
  Surface() = default;

  /// @brief Constructs a surface with the given width, height, and font.
  /// @param w Surface width in pixels
  /// @param h Surface height in pixels
  /// @param font Reference to the font used for text rendering
  Surface(size_t w, size_t h, IFont<RGB_T>& font)
      : SurfaceBase<RGB_T>(w, h, font) {}

  /// @brief Sets the pixel at (x, y) to the specified color.
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @param color Pixel color value
  void setPixel(size_t x, size_t y, RGB_T color) override {
    buffer[(y * width_) + x] = color;
  }

  /// @brief Gets the color of the pixel at (x, y).
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @return Pixel color value
  RGB_T getPixel(size_t x, size_t y) const override {
    return buffer[(y * width_) + x];
  }

  /// @brief Resizes the internal pixel buffer to the specified width and
  /// height.
  /// @param w New width
  /// @param h New height
  void resizeBuffer(size_t w, size_t h) override { buffer.resize(w * h); }

  /// @brief Returns a pointer to the raw data as bytes.
  /// @return Pointer to the buffer as uint8_t
  const uint8_t* data() const override {
    return reinterpret_cast<const uint8_t*>(buffer.data());
  }

  /// @brief Returns the size of the buffer in bytes.
  /// @return Buffer size in bytes
  size_t size() const override { return buffer.size() * RGB_T::size() / 8; }

 protected:
  std::vector<RGB_T> buffer;
};

/// @brief Alias for a sprite surface with the same pixel format as the main surface.
template <typename RGB_T = RGB565>
using Sprite = Surface<RGB_T>;


}  // namespace tinygpu