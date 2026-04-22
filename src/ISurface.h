#pragma once

#include <stddef.h>
#include <stdint.h>


#include "RGB565.h"

namespace tinygpu {


// Forward declaration of IFont to avoid circular dependency.
template <typename RGB_T> class IFont;

/**
 * @brief Abstract framebuffer and drawing interface for TinyGPU surfaces.
 *
 * Implementations expose pixel access, primitive drawing, sprite operations,
 * and text rendering through a common API.
 *
 * @note The coordinate x=0 and y=0 corresponds to the top-left corner of the
 * surface. The x coordinate increases to the right and the y coordinate
 * increases downwards.
 */
template <typename RGB_T = RGB565>
class ISurface {
 public:
  /// Destroys the framebuffer interface.
  virtual ~ISurface() = default;

  /// Resizes the framebuffer surface.
  virtual bool resize(size_t newWidth, size_t newHeight) = 0;
  /// Sets a pixel at the given position.
  virtual void setPixel(size_t x, size_t y, RGB_T color) = 0;
  /// Returns the pixel at the given position.
  virtual RGB_T getPixel(size_t x, size_t y) const = 0;
  /// Scrolls the framebuffer content by the specified offsets.
  virtual void scroll(int dx, int dy) = 0;
  /// Clears the framebuffer with a single color.
  virtual void clear(RGB_T color = RGB_T(0)) = 0;
  /// Draws a line between two points.
  virtual void drawLine(size_t x0, size_t y0, size_t x1, size_t y1,
                        RGB_T color) = 0;
  /// Draws a rectangle outline.
  virtual void drawRect(size_t x, size_t y, size_t w, size_t h,
                        RGB_T color) = 0;
  /// Fills a rectangle.
  virtual void fillRect(size_t x, size_t y, size_t w, size_t h,
                        RGB_T color) = 0;
  /// Draws a circle outline.
  virtual void drawCircle(size_t x, size_t y, size_t r, RGB_T color) = 0;
  /// Fills a circle.
  virtual void fillCircle(size_t x, size_t y, size_t r, RGB_T color) = 0;
  /// Draws a sprite using an optional transparent color.
  virtual void drawSprite(size_t x, size_t y, const ISurface<RGB_T>& sprite,
                          RGB_T invisibleColor = RGB_T(0)) = 0;
  /// Clears the area covered by a sprite.
  virtual void clearSprite(size_t x, size_t y, ISurface<RGB_T>& sprite,
                           RGB_T clearColor = RGB_T(0)) = 0;
  /// Copies pixels from the framebuffer into a sprite.
  virtual void copySprite(size_t x, size_t y, const ISurface<RGB_T>& sprite) = 0;
  /// Returns the framebuffer width in pixels.
  virtual size_t width() const = 0;
  /// Returns the framebuffer height in pixels.
  virtual size_t height() const = 0;
  /// Draws UTF-8 text using the configured font.
  virtual void drawText(int16_t x, int16_t y, const char* text,
                        RGB_T foreground, RGB_T background = RGB_T(0),
                        bool opaque = false, uint8_t scale = 1,
                        uint8_t spacing = 1, uint8_t lineSpacing = 1) = 0;

  /// Returns the currently set font for text rendering.
  virtual IFont<RGB_T>& font() = 0;
  /// Provides access to the framebuffer as a byte buffer.
  virtual const uint8_t* data() const = 0;
  /// Provides the total data size in bytes.
  virtual size_t size() const = 0;
};


}  // namespace tinygpu
