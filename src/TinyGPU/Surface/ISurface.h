#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>


#include "TinyGPU/Color/RGB565.h"

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

  /// Initializes the framebuffer surface.
  virtual bool begin() = 0;
  /// Closes the framebuffer and releases resources.
  virtual void end() = 0;

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

  /// Draws a rectangle outline with rounded corners.
  ///
  /// @param radius Corner radius in pixels; clamped to min(w,h)/2.
  ///
  /// Implemented here (not left pure virtual) purely in terms of drawLine(),
  /// setPixel() and contains(), so every existing ISurface implementation
  /// gets a correct default for free. Override only to provide a faster,
  /// hardware-specific version.
  virtual void drawRoundRect(size_t x, size_t y, size_t w, size_t h,
                             size_t radius, RGB_T color) {
    if (w == 0 || h == 0) return;
    const size_t maxRadius = (w < h ? w : h) / 2;
    if (radius > maxRadius) radius = maxRadius;
    if (radius == 0) {
      drawRect(x, y, w, h, color);
      return;
    }

    drawLine(x + radius, y, x + w - 1 - radius, y, color);
    drawLine(x + radius, y + h - 1, x + w - 1 - radius, y + h - 1, color);
    drawLine(x, y + radius, x, y + h - 1 - radius, color);
    drawLine(x + w - 1, y + radius, x + w - 1, y + h - 1 - radius, color);

    plotRoundRectCorner(x + radius, y + radius, radius,
                        RoundRectCorner::kTopLeft, color);
    plotRoundRectCorner(x + w - 1 - radius, y + radius, radius,
                        RoundRectCorner::kTopRight, color);
    plotRoundRectCorner(x + w - 1 - radius, y + h - 1 - radius, radius,
                        RoundRectCorner::kBottomRight, color);
    plotRoundRectCorner(x + radius, y + h - 1 - radius, radius,
                        RoundRectCorner::kBottomLeft, color);
  }

  /// Fills a rectangle with rounded corners.
  ///
  /// @param radius Corner radius in pixels; clamped to min(w,h)/2.
  ///
  /// Row-scan fill: each row's horizontal inset comes from the corner
  /// circle's equation (0 for the straight middle rows), then the existing
  /// fillRect() draws that single row. Produces the same shape as the
  /// classic "middle band + two rounded end caps" decomposition, just
  /// expressed as one loop. Implemented here (not pure virtual) so every
  /// existing ISurface implementation gets it for free.
  virtual void fillRoundRect(size_t x, size_t y, size_t w, size_t h,
                             size_t radius, RGB_T color) {
    if (w == 0 || h == 0) return;
    const size_t maxRadius = (w < h ? w : h) / 2;
    if (radius > maxRadius) radius = maxRadius;
    if (radius == 0) {
      fillRect(x, y, w, h, color);
      return;
    }

    for (size_t row = 0; row < h; ++row) {
      size_t dy = 0;
      if (row < radius) {
        dy = radius - row;
      } else if (row >= h - radius) {
        dy = radius - (h - 1 - row);
      }

      size_t inset = 0;
      if (dy > 0) {
        const double dx = sqrt(static_cast<double>(radius) * radius -
                               static_cast<double>(dy) * dy);
        inset = radius - static_cast<size_t>(dx);
      }

      const size_t rowWidth = (w > 2 * inset) ? (w - 2 * inset) : 0;
      if (rowWidth > 0) {
        fillRect(x + inset, y + row, rowWidth, 1, color);
      }
    }
  }

  /// Draws a stroked arc around center (cx, cy) with radius r, sweeping from
  /// startDeg to endDeg (degrees, clockwise on screen since y increases
  /// downward; 0 deg = +X/3 o'clock direction; pass endDeg < startDeg to
  /// wrap through 0, e.g. 350 -> 370). thickness draws that many concentric
  /// 1px radii ending at r. Used for circular progress/spinner widgets.
  virtual void drawArc(size_t cx, size_t cy, size_t r, float startDeg,
                       float endDeg, RGB_T color, uint8_t thickness = 1) {
    if (r == 0) return;
    if (thickness == 0) thickness = 1;
    const size_t minRadius =
        (static_cast<size_t>(thickness) - 1 >= r)
            ? 1
            : (r - (static_cast<size_t>(thickness) - 1));

    constexpr float kPi = 3.14159265358979323846f;
    float startRad = startDeg * kPi / 180.0f;
    float endRad = endDeg * kPi / 180.0f;
    if (endRad < startRad) endRad += 2.0f * kPi;

    for (size_t radius = minRadius; radius <= r; ++radius) {
      const float step = 1.0f / static_cast<float>(radius);
      for (float angle = startRad; angle <= endRad; angle += step) {
        const long px =
            static_cast<long>(cx) +
            static_cast<long>(lroundf(cosf(angle) * static_cast<float>(radius)));
        const long py =
            static_cast<long>(cy) +
            static_cast<long>(lroundf(sinf(angle) * static_cast<float>(radius)));
        if (px >= 0 && py >= 0 &&
            contains(static_cast<size_t>(px), static_cast<size_t>(py))) {
          setPixel(static_cast<size_t>(px), static_cast<size_t>(py), color);
        }
      }
    }
  }

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
  ///Checks if the given coordinates are within the surface bounds.
  virtual bool contains(size_t x, size_t y) = 0;

 private:
  /// Which corner a plotRoundRectCorner() arc belongs to; selects which
  /// quadrant of the midpoint-circle loop's symmetric points to plot.
  enum class RoundRectCorner : uint8_t {
    kTopLeft,
    kTopRight,
    kBottomRight,
    kBottomLeft
  };

  /// Plots one quarter-circle arc for a drawRoundRect() corner, using the
  /// same midpoint-circle loop as SurfaceBase::drawCircle, restricted to the
  /// single quadrant matching `corner` so the 4 arcs tile the full rounded
  /// rectangle outline with no gaps or overlap.
  void plotRoundRectCorner(size_t cx, size_t cy, size_t r,
                           RoundRectCorner corner, RGB_T color) {
    long offsetX = static_cast<long>(r);
    long offsetY = 0;
    long decision = 1 - offsetX;

    while (offsetX >= offsetY) {
      long px1, py1, px2, py2;
      switch (corner) {
        case RoundRectCorner::kTopLeft:
          px1 = static_cast<long>(cx) - offsetX;
          py1 = static_cast<long>(cy) - offsetY;
          px2 = static_cast<long>(cx) - offsetY;
          py2 = static_cast<long>(cy) - offsetX;
          break;
        case RoundRectCorner::kTopRight:
          px1 = static_cast<long>(cx) + offsetY;
          py1 = static_cast<long>(cy) - offsetX;
          px2 = static_cast<long>(cx) + offsetX;
          py2 = static_cast<long>(cy) - offsetY;
          break;
        case RoundRectCorner::kBottomRight:
          px1 = static_cast<long>(cx) + offsetX;
          py1 = static_cast<long>(cy) + offsetY;
          px2 = static_cast<long>(cx) + offsetY;
          py2 = static_cast<long>(cy) + offsetX;
          break;
        case RoundRectCorner::kBottomLeft:
        default:
          px1 = static_cast<long>(cx) - offsetY;
          py1 = static_cast<long>(cy) + offsetX;
          px2 = static_cast<long>(cx) - offsetX;
          py2 = static_cast<long>(cy) + offsetY;
          break;
      }

      if (px1 >= 0 && py1 >= 0 &&
          contains(static_cast<size_t>(px1), static_cast<size_t>(py1))) {
        setPixel(static_cast<size_t>(px1), static_cast<size_t>(py1), color);
      }
      if (px2 >= 0 && py2 >= 0 &&
          contains(static_cast<size_t>(px2), static_cast<size_t>(py2))) {
        setPixel(static_cast<size_t>(px2), static_cast<size_t>(py2), color);
      }

      ++offsetY;
      if (decision <= 0) {
        decision += 2 * offsetY + 1;
      } else {
        --offsetX;
        decision += 2 * (offsetY - offsetX) + 1;
      }
    }
  }
};


}  // namespace tinygpu
