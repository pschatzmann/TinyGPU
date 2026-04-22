#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ISurface.h"
#include "IFont.h"

namespace tinygpu {

/**
 * @brief Bottom-left Cartesian coordinate view over an existing Surface.
 *
 * CartesianView wraps a Surface and remaps the logical y-axis so that
 * `(0, 0)` refers to the lower-left corner while preserving the wrapped
 * surface storage and export format.
 */

template <typename RGB_T = RGB565>
class CartesianView : public ISurface<RGB_T> {
 public:
  /// Creates a Cartesian view over the provided surface.
  explicit CartesianView(ISurface<RGB_T>& surface) : surface_(surface) {}

  /// Resizes the wrapped surface.
  void resize(size_t newWidth, size_t newHeight) override {
    surface_.resize(newWidth, newHeight);
  }

  /// Sets a pixel using Cartesian bottom-left coordinates.
  void setPixel(size_t x, size_t y, RGB_T color) override {
    surface_.setPixel(x, mappedY(y), color);
  }

  /// Returns a pixel using Cartesian bottom-left coordinates.
  RGB_T getPixel(size_t x, size_t y) const override {
    return surface_.getPixel(x, mappedY(y));
  }

  /// Clears the wrapped surface.
  void clear(RGB_T color = RGB_T(0)) override { surface_.clear(color); }

  /// Draws a line using Cartesian bottom-left coordinates.
  void drawLine(size_t x0, size_t y0, size_t x1, size_t y1,
                RGB_T color) override {
    surface_.drawLine(x0, mappedY(y0), x1, mappedY(y1), color);
  }

  /// Draws a rectangle outline using Cartesian bottom-left coordinates.
  void drawRect(size_t x, size_t y, size_t w, size_t h, RGB_T color) override {
    surface_.drawRect(x, mappedTopOfBox(y, h), w, h, color);
  }

  /// Fills a rectangle using Cartesian bottom-left coordinates.
  void fillRect(size_t x, size_t y, size_t w, size_t h, RGB_T color) override {
    surface_.fillRect(x, mappedTopOfBox(y, h), w, h, color);
  }

  /// Draws a circle outline using Cartesian bottom-left coordinates.
  void drawCircle(size_t x, size_t y, size_t r, RGB_T color) override {
    surface_.drawCircle(x, mappedY(y), r, color);
  }

  /// Fills a circle using Cartesian bottom-left coordinates.
  void fillCircle(size_t x, size_t y, size_t r, RGB_T color) override {
    surface_.fillCircle(x, mappedY(y), r, color);
  }

  /// Draws a sprite with its lower-left corner anchored at the given position.
  void drawSprite(size_t x, size_t y, const ISurface<RGB_T>& sprite,
                  RGB_T invisibleColor = RGB_T(0)) override {
    surface_.drawSprite(x, mappedTopOfBox(y, sprite.height()), sprite,
                        invisibleColor);
  }

  /// Clears the area occupied by a sprite anchored at its lower-left corner.
  void clearSprite(size_t x, size_t y, ISurface<RGB_T>& sprite,
                   RGB_T clearColor = RGB_T(0)) override {
    surface_.clearSprite(x, mappedTopOfBox(y, sprite.height()), sprite,
                         clearColor);
  }

  /// Copies pixels from the view into a sprite using Cartesian coordinates.
  void copySprite(size_t x, size_t y, const ISurface<RGB_T>& sprite) override {
    surface_.copySprite(x, mappedTopOfBox(y, sprite.height()), sprite);
  }

  /// Returns the wrapped surface width.
  size_t width() const override { return surface_.width(); }

  /// Returns the wrapped surface height.
  size_t height() const override { return surface_.height(); }

  /// Draws text with the given lower-left text box origin.
  void drawText(int16_t x, int16_t y, const char* text, RGB565 foreground,
                RGB565 background = RGB565(0), bool opaque = false,
                uint8_t scale = 1, uint8_t spacing = 1,
                uint8_t lineSpacing = 1) override {
    const size_t textHeight =
        surface_.font().measureTextHeight(text, scale, lineSpacing);
    surface_.drawText(x,
                      static_cast<int16_t>(
                          mappedTopOfBox(static_cast<size_t>(y), textHeight)),
                      text, foreground, background, opaque, scale, spacing,
                      lineSpacing);
  }

  /// Returns the wrapped byte buffer.
  const uint8_t* data() const override { return surface_.data(); }

  /// Returns the wrapped buffer size in bytes.
  size_t size() const override { return surface_.size(); }

  /// Returns the wrapped surface as a reference.
  ISurface<RGB_T>& surface() { return surface_; }

  /// Returns the wrapped surface as a const reference.
  const ISurface<RGB_T>& surface() const { return surface_; }

  /// Returns the font of the wrapped surface.
  IFont<RGB_T>& font() override { return surface_.font(); }

 protected:
  ISurface<RGB_T>& surface_;

  /// Maps a logical Cartesian Y coordinate (bottom-left origin) to the wrapped
  /// surface's Y (top-left origin).
  size_t mappedY(size_t y) const { return (height() - 1) - y; }

  /**
   * @brief Maps the logical bottom-left Y and object height to the top Y
   * coordinate for box-based operations.
   *
   * For rectangles, sprites, and text, this computes the top Y (in Cartesian
   * coordinates), then maps it to the wrapped surface's Y. Returns 0 if the
   * computed top Y is out of bounds.
   *
   * @param y Logical bottom Y (Cartesian, bottom-left origin)
   * @param objectHeight Height of the object (rectangle, sprite, or text)
   * @return size_t Mapped Y coordinate for the top of the box in the wrapped
   * surface
   */
  size_t mappedTopOfBox(size_t y, size_t objectHeight) const {
    if (height() == 0 || objectHeight == 0) {
      return 0;
    }

    const uint64_t topY = static_cast<uint64_t>(y) + objectHeight - 1U;
    if (topY >= height()) {
      return 0;
    }
    return mappedY(static_cast<size_t>(topY));
  }
};

}  // namespace tinygpu