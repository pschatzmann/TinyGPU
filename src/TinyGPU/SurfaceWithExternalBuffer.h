#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <cstddef>

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
#include "TinyGPU/Vector.h"

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
class SurfaceWithExternalBuffer : public SurfaceBase<RGB_T> {
  static_assert(!std::is_same<RGB_T, bool>::value,
                "Use SurfaceMonochrome for monochrome surfaces");

 public:
  using SurfaceBase<RGB_T>::width_;
  using SurfaceBase<RGB_T>::height_;
  using SurfaceBase<RGB_T>::font_;
  using SurfaceBase<RGB_T>::linePrinter_;
  /// @brief Default constructor. Creates an empty surface.
  SurfaceWithExternalBuffer() = default;

  /// @brief Constructs a surface with the given width, height, and font.
  /// @param w Surface width in pixels
  /// @param h Surface height in pixels
  /// @param font Reference to the font used for text rendering
  SurfaceWithExternalBuffer(size_t w, size_t h, IFont<RGB_T>& font = FontRGB565)
      : SurfaceBase<RGB_T>(w, h, font) {}

  /// @brief Sets the pixel at (x, y) to the specified color.
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @param color Pixel color value
  void setPixel(size_t x, size_t y, RGB_T color) override {
    if (buffer == nullptr) return;
    if (x >= width_ || y >= height_) return; // Bounds check
    buffer[(y * width_) + x] = color;
  }

  /// @brief Gets the color of the pixel at (x, y).
  /// @param x X coordinate
  /// @param y Y coordinate
  /// @return Pixel color value
  RGB_T getPixel(size_t x, size_t y) const override {
    if (buffer == nullptr) return RGB_T();
    if (x >= width_ || y >= height_) return RGB_T(); // Bounds check
    return buffer[(y * width_) + x];
  }

  /// @brief Resizes the internal pixel buffer to the specified width and
  /// height.
  /// @param w New width
  /// @param h New height
  bool resizeBuffer(size_t w, size_t h) override {
    buffer_size = w * h * sizeof(RGB_T);
    assert(buffer_size <= buffer_capacity);
    return true;
  }

  /// @brief Returns a pointer to the raw data as bytes.
  /// @return Pointer to the buffer as uint8_t
  const uint8_t* data() const override {
    return reinterpret_cast<const uint8_t*>(buffer);
  }

  /// @brief Mutable counterpart of data() - a pointer to the raw pixel
  /// buffer as bytes, for callers that want to fill it directly (e.g. a
  /// codec's own RGB565/RGB666/RGB888 conversion output) instead of
  /// going through setPixel() one pixel at a time. Same bytes data()
  /// exposes read-only; writing size() bytes through this pointer is
  /// exactly equivalent to that many setPixel() calls.
  uint8_t* data() { return reinterpret_cast<uint8_t*>(buffer); }

  /// @brief Mutable pointer to the pixel buffer, typed as RGB_T rather
  /// than raw bytes - the same buffer data() above exposes, just typed
  /// for direct pixel-at-a-time indexing without a cast. RGB_T's binary
  /// layout matches a plain packed integer of the same width for every
  /// format this library ships (e.g. RGB565 <-> uint16_t), so a caller
  /// producing raw packed values can reinterpret_cast this pointer to
  /// that type if it doesn't want to construct RGB_T instances directly.
  RGB_T* pixels() { return buffer; }

  /// @brief Number of RGB_T entries pixels()/data() has room for
  /// (width() * height()) - distinct from size(), which is that count
  /// converted to bytes.
  size_t pixelCount() const { return buffer_size / sizeof(RGB_T); }

  /// @brief Returns the size of the buffer in bytes.
  /// @return Buffer size in bytes
  size_t size() const override { return buffer_size; }

  /// Defines the external buffer
  void setExternalBuffer(void* externalBuffer, size_t capacityBytes) {
    buffer = static_cast<RGB_T*>(externalBuffer)    ;
    buffer_capacity = capacityBytes;
  }

  /// Returns the capacity of the external buffer in bytes.
  size_t capacity() const { return buffer_capacity; }

 protected:
  RGB_T* buffer = nullptr;
  size_t buffer_size = 0; // in bytes
  size_t buffer_capacity = 0; // in bytes
};


}  // namespace tinygpu