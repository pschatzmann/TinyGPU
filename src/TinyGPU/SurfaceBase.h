#pragma once
#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <vector>

#include "IFont.h"
#include "ISurface.h"
#include "LinePrinter.h"

namespace tinygpu {

/**
 * @brief Base class for 2D surfaces with drawing and text rendering support.
 *
 * SurfaceBase provides common logic for managing surface dimensions, font
 * handling, and basic geometry operations. It is intended to be subclassed by
 * concrete surface implementations that provide pixel storage and access (e.g.,
 * Surface, SurfaceMonochrome).
 *
 * @tparam PixelT The pixel color type (e.g., RGB565, bool, etc.)
 */
template <typename PixelT>
class SurfaceBase : public ISurface<PixelT> {
 public:
  /// Default constructor.
  SurfaceBase() = default;

  /// Construct with width, height, and font.
  SurfaceBase(size_t width, size_t height, IFont<PixelT>& font) : font_(&font) {
    setFont(font);
    width_ = width;
    height_ = height;
  }

  /// Initializes the surface by resizing it to the current dimensions.
  bool begin() {
    return resize(width_, height_);
  }

  /// Clears the surface and releases any allocated resources.
  void end() {
     resizeBuffer(0, 0);
  }

  /// Virtual destructor.
  ~SurfaceBase() override = default;

  /// Abstract method: set a pixel at (x, y) to the specified color.
  void setPixel(size_t x, size_t y, PixelT color) override = 0;

  /// Abstract method: get the pixel color at (x, y).
  PixelT getPixel(size_t x, size_t y) const override = 0;

  /// Set the font for text rendering.
  void setFont(IFont<PixelT>& font) {
    linePrinter_.setFont(font);
    font_ = &font;
  }

  /// Get the current font (mutable).
  IFont<PixelT>& font() override { return *font_; }

  /// Get the current font (const).
  const IFont<PixelT>& font() const { return *font_; }

  /// Resize the surface to new dimensions.
  bool resize(size_t newWidth, size_t newHeight) override {
    width_ = newWidth;
    height_ = newHeight;
    return resizeBuffer(newWidth, newHeight);
  }

  /// Get the width of the surface.
  size_t width() const override { return width_; }

  /// Get the height of the surface.
  size_t height() const override { return height_; }

  // Pixel access (to be implemented by derived classes)
  virtual bool resizeBuffer(size_t, size_t) = 0;

  // Drawing/geometry logic (now generic, uses setPixel/getPixel)
  void clear(PixelT color = PixelT()) override {
    for (size_t y = 0; y < height_; ++y) {
      for (size_t x = 0; x < width_; ++x) {
        setPixel(x, y, color);
      }
    }
  }

  /// Scroll the surface content by (dx, dy) pixels.
  void scroll(int dx, int dy) {
    if (dx == 0 && dy == 0) return;
    Vector<PixelT> newBuffer(width_ * height_);
    for (size_t y = 0; y < height_; ++y) {
      for (size_t x = 0; x < width_; ++x) {
        int srcX = (int)x + dx;
        int srcY = (int)y + dy;
        PixelT color = (srcX >= 0 && srcX < (int)width_ && srcY >= 0 &&
                        srcY < (int)height_)
                           ? getPixel(srcX, srcY)
                           : PixelT();
        newBuffer[y * width_ + x] = color;
      }
    }
    // Copy new buffer back to the surface
    for (size_t i = 0; i < newBuffer.size(); ++i) {
      setPixel(i % width_, i / width_, newBuffer[i]);
    }
  }

  /// Draw a line from (x0, y0) to (x1, y1) with the given color.
  void drawLine(size_t x0, size_t y0, size_t x1, size_t y1,
                PixelT color) override {
    int dx = std::abs((int)x1 - (int)x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs((int)y1 - (int)y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;
    while (true) {
      setPixelClipped(x0, y0, color);
      if (x0 == x1 && y0 == y1) break;
      e2 = 2 * err;
      if (e2 >= dy) {
        err += dy;
        x0 += sx;
      }
      if (e2 <= dx) {
        err += dx;
        y0 += sy;
      }
    }
  }

  /// Draw a rectangle outline at (x, y) with width w, height h, and color.
  void drawRect(size_t x, size_t y, size_t w, size_t h, PixelT color) override {
    if (w == 0 || h == 0) return;
    size_t x1 = x + w - 1, y1 = y + h - 1;
    drawLine(x, y, x1, y, color);
    drawLine(x, y1, x1, y1, color);
    drawLine(x, y, x, y1, color);
    drawLine(x1, y, x1, y1, color);
  }

  /// Fill a rectangle at (x, y) with width w, height h, and color.
  void fillRect(size_t x, size_t y, size_t w, size_t h, PixelT color) override {
    if (w == 0 || h == 0) return;
    size_t endX = std::min(x + w, width_);
    size_t endY = std::min(y + h, height_);
    for (size_t yy = y; yy < endY; ++yy) {
      for (size_t xx = x; xx < endX; ++xx) {
        setPixel(xx, yy, color);
      }
    }
  }

  /// Draw a circle outline centered at (x, y) with radius r and color.
  void drawCircle(size_t x, size_t y, size_t r, PixelT color) override {
    int offsetX = r, offsetY = 0, decision = 1 - (int)offsetX;
    while (offsetX >= offsetY) {
      setPixelClipped(x + offsetX, y + offsetY, color);
      setPixelClipped(x + offsetY, y + offsetX, color);
      setPixelClipped(x - offsetY, y + offsetX, color);
      setPixelClipped(x - offsetX, y + offsetY, color);
      setPixelClipped(x - offsetX, y - offsetY, color);
      setPixelClipped(x - offsetY, y - offsetX, color);
      setPixelClipped(x + offsetY, y - offsetX, color);
      setPixelClipped(x + offsetX, y - offsetY, color);
      ++offsetY;
      if (decision <= 0)
        decision += 2 * offsetY + 1;
      else {
        --offsetX;
        decision += 2 * (offsetY - offsetX) + 1;
      }
    }
  }

  /// Fill a circle centered at (x, y) with radius r and color.
  void fillCircle(size_t x, size_t y, size_t r, PixelT color) override {
    int offsetX = r, offsetY = 0, decision = 1 - (int)offsetX;
    while (offsetX >= offsetY) {
      drawHorizontalLineClipped(x - offsetX, x + offsetX, y + offsetY, color);
      drawHorizontalLineClipped(x - offsetX, x + offsetX, y - offsetY, color);
      drawHorizontalLineClipped(x - offsetY, x + offsetY, y + offsetX, color);
      drawHorizontalLineClipped(x - offsetY, x + offsetY, y - offsetX, color);
      ++offsetY;
      if (decision <= 0)
        decision += 2 * offsetY + 1;
      else {
        --offsetX;
        decision += 2 * (offsetY - offsetX) + 1;
      }
    }
  }

  /// Draw a sprite at (x, y), skipping pixels matching invisibleColor.
  void drawSprite(size_t x, size_t y, const ISurface<PixelT>& sprite,
                  PixelT invisibleColor = PixelT()) override {
    for (size_t spriteY = 0; spriteY < sprite.height(); ++spriteY) {
      for (size_t spriteX = 0; spriteX < sprite.width(); ++spriteX) {
        PixelT color = sprite.getPixel(spriteX, spriteY);
        if (color != invisibleColor) {
          setPixelClipped(x + spriteX, y + spriteY, color);
        }
      }
    }
  }

  /// Clear the region covered by a sprite at (x, y) to clearColor.
  void clearSprite(size_t x, size_t y, ISurface<PixelT>& sprite,
                   PixelT clearColor = PixelT()) override {
    for (size_t spriteY = 0; spriteY < sprite.height(); ++spriteY) {
      for (size_t spriteX = 0; spriteX < sprite.width(); ++spriteX) {
        setPixelClipped(x + spriteX, y + spriteY, clearColor);
      }
    }
  }

  /// Copy the framebuffer region at (x, y) into the sprite.
  void copySprite(size_t x, size_t y, const ISurface<PixelT>& sprite) override {
    for (size_t spriteY = 0; spriteY < sprite.height(); ++spriteY) {
      for (size_t spriteX = 0; spriteX < sprite.width(); ++spriteX) {
        size_t framebufferX = x + spriteX;
        size_t framebufferY = y + spriteY;
        PixelT color = (framebufferX < width_ && framebufferY < height_)
                           ? getPixel(framebufferX, framebufferY)
                           : PixelT();
        const_cast<ISurface<PixelT>&>(sprite).setPixel(spriteX, spriteY, color);
      }
    }
  }

  /// Draw text at (x, y) with foreground/background color and formatting
  /// options.
  void drawText(int16_t x, int16_t y, const char* text, PixelT foreground,
                PixelT background = PixelT(), bool opaque = false,
                uint8_t scale = 1, uint8_t spacing = 1,
                uint8_t lineSpacing = 1) override {
    font_->drawText(*this, x, y, text, foreground, background, opaque, scale,
                    spacing, lineSpacing);
  }

  /// Get the line printer for this surface.
  LinePrinter<PixelT>& linePrinter() {
    linePrinter_.setFont(*font_);
    linePrinter_.setTarget(*this);
    return linePrinter_;
  }

  /// Returns true if (x, y) is within the surface bounds.
  bool isInBounds(size_t x, size_t y) const {
    return x < width_ && y < height_;
  }
  /// Set a pixel only if (x, y) is in bounds.
  void setPixelClipped(size_t x, size_t y, PixelT color) {
    if (isInBounds(x, y)) setPixel(x, y, color);
  }
  /// Draw a horizontal line from x0 to x1 at y, clipped to bounds.
  void drawHorizontalLineClipped(int x0, int x1, int y, PixelT color) {
    if (y < 0 || (size_t)y >= height_) return;
    int startX = std::max(0, std::min(x0, x1));
    int endX = std::min((int)width_ - 1, std::max(x0, x1));
    if (startX > endX) return;
    for (int x = startX; x <= endX; ++x) setPixel(x, y, color);
  }

 protected:
  size_t width_ = 0;
  size_t height_ = 0;
  IFont<PixelT>* font_ = nullptr;
  LinePrinter<PixelT> linePrinter_;
};

}  // namespace tinygpu
