
#pragma once
#include <algorithm>
#include <cmath>
#include <memory>

#include "Surface.h"
#include "TinyGPULogger.h"
#include "TinyGPUConfig.h"
#include "TinyGPU/Vector.h"

namespace tinygpu {

/**
 * @brief Framebuffer with sprite placement and background restoration support.
 *
 * The class extends TinyGPU with sprite bookkeeping so sprites can be added,
 * moved, scaled, and removed while preserving the pixels behind them.
 */
template <typename RGB_T = RGB565, typename SurfaceT = Surface<RGB_T>>
class FrameBuffer : public ISurface<RGB_T> {
 public:
  /**
   * Tracks a sprite instance together with its saved background pixels.
   *
   * Each SpriteInfo stores the sprite position, transparent color, current
   * sprite image, and a snapshot of the framebuffer region covered by it.
   */
  struct SpriteInfo {
    size_t x = 0;
    size_t y = 0;
    size_t maxWidth = 0;
    size_t maxHeight = 0;
    RGB_T invisibleColor = RGB_T(0);
    const ISurface<RGB_T>* sprite = nullptr;
    std::unique_ptr<SurfaceT> transformedSprite;
    SurfaceT originalPixels;
    IFont<RGB_T>& fontRef;

    SpriteInfo(size_t startX, size_t startY,
               const ISurface<RGB_T>& sourceSprite, RGB_T transparentColor,
               IFont<RGB_T>& font)
        : x(startX),
          y(startY),
          invisibleColor(transparentColor),
          sprite(&sourceSprite),
          originalPixels(sourceSprite.width(), sourceSprite.height(), font),
          fontRef(font) {}

    /// Returns the sprite image currently used for drawing.
    const ISurface<RGB_T>& currentSprite() const {
      return transformedSprite
                 ? static_cast<const ISurface<RGB_T>&>(*transformedSprite)
                 : *sprite;
    }

    /// Saves the background pixels currently covered by the sprite.
    void saveOriginalPixels(ISurface<RGB_T>& framebuffer) {
      originalPixels.resize(currentSprite().width(), currentSprite().height());
      framebuffer.copySprite(x, y, originalPixels);
    }

    /// Set the maximum buffer size for transformedSprite and allocate buffer.
    void setMaxSize(size_t maxX, size_t maxY) {
      maxWidth = maxX;
      maxHeight = maxY;
      if (!transformedSprite) {
        transformedSprite =
            std::make_unique<SurfaceT>(maxWidth, maxHeight, fontRef);
        transformedSprite->begin();
      }
    }

    /// Replaces the current transformed sprite image, using only the allocated
    /// buffer.
    void setTransformedSprite(SurfaceT&& newSprite) {
      if (!transformedSprite) {
        // If max size not set, use current size as max
        maxWidth = newSprite.width();
        maxHeight = newSprite.height();
        transformedSprite =
            std::make_unique<SurfaceT>(maxWidth, maxHeight, newSprite.font());
        transformedSprite->begin();
      }
      // Only copy the region that fits
      size_t copyWidth = std::min(maxWidth, newSprite.width());
      size_t copyHeight = std::min(maxHeight, newSprite.height());
      for (size_t y = 0; y < copyHeight; ++y) {
        for (size_t x = 0; x < copyWidth; ++x) {
          transformedSprite->setPixel(x, y, newSprite.getPixel(x, y));
        }
      }
      // Optionally clear the rest if newSprite is smaller than max
      for (size_t y = copyHeight; y < maxHeight; ++y) {
        for (size_t x = 0; x < maxWidth; ++x) {
          transformedSprite->setPixel(x, y, RGB_T(0));
        }
      }
      for (size_t y = 0; y < copyHeight; ++y) {
        for (size_t x = copyWidth; x < maxWidth; ++x) {
          transformedSprite->setPixel(x, y, RGB_T(0));
        }
      }
    }
  };

  /// Creates an empty framebuffer.
  FrameBuffer() = default;

  /// Creates a framebuffer with the specified size and font.
  FrameBuffer(size_t width, size_t height, IFont<RGB_T>& font)
      : surface_(width, height, font) {}

  bool begin() override { return surface_.begin(); }
  void end() override { surface_.end(); }

  /// Adds a sprite to the framebuffer and draws it at the given position.
  SpriteInfo& addSprite(size_t x, size_t y, const ISurface<RGB_T>& sprite,
                        RGB_T invisibleColor = RGB_T(0)) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO, "Adding sprite at (%zu, %zu)",
                      x, y);
    auto info = std::make_unique<SpriteInfo>(x, y, sprite, invisibleColor,
                                             activeFont());

    info->saveOriginalPixels(surface_);
    surface_.drawSprite(x, y, info->currentSprite(), invisibleColor);

    sprites_.push_back(std::move(info));
    return *sprites_.back();
  }

  /// Adds a sprite with a preallocated max buffer size for transformations.
  SpriteInfo& addSprite(size_t x, size_t y, size_t maxX, size_t maxY, 
                        const ISurface<RGB_T>& sprite,
                        RGB_T invisibleColor = RGB_T(0)) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Adding sprite at (%zu, %zu) with max size (%zu, %zu)", x,
                      y, maxX, maxY);
    auto info = std::make_unique<SpriteInfo>(x, y, sprite, invisibleColor,
                                             activeFont());
    info->setMaxSize(maxX, maxY);
    info->saveOriginalPixels(surface_);
    surface_.drawSprite(x, y, info->currentSprite(), invisibleColor);
    sprites_.push_back(std::move(info));
    return *sprites_.back();
  }

  /// Removes a sprite and restores the pixels behind it.
  void removeSprite(SpriteInfo& spriteInfo) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO, "Removing sprite at (%zu, %zu)",
                      spriteInfo.x, spriteInfo.y);
    restoreOriginalPixels(spriteInfo);
    auto it = findSprite(spriteInfo);
    if (it != sprites_.end()) {
      sprites_.erase(it);
    }
  }

  /// Moves a sprite to a new position and redraws it.
  void moveSprite(SpriteInfo& spriteInfo, size_t newX, size_t newY) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Moving sprite from (%zu, %zu) to (%zu, %zu)",
                      spriteInfo.x, spriteInfo.y, newX, newY);
    if (spriteInfo.x == newX && spriteInfo.y == newY) {
      return;
    }

    const Rect oldBounds{spriteInfo.x, spriteInfo.y,
                         spriteInfo.currentSprite().width(),
                         spriteInfo.currentSprite().height()};
    const Rect newBounds{newX, newY, spriteInfo.currentSprite().width(),
                         spriteInfo.currentSprite().height()};
    const Rect overlap = intersect(oldBounds, newBounds);

    restoreExposedPixels(spriteInfo, oldBounds, overlap);
    Surface<RGB_T> movedBackground =
        captureUpdatedBackground(spriteInfo, oldBounds, newBounds, overlap);

    spriteInfo.x = newX;
    spriteInfo.y = newY;
    spriteInfo.originalPixels = std::move(movedBackground);
    surface_.drawSprite(spriteInfo.x, spriteInfo.y, spriteInfo.currentSprite(),
                        spriteInfo.invisibleColor);
  }

  /// Scales a sprite image and redraws it at its current position.
  void scaleSprite(SpriteInfo& spriteInfo, float scale) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Scaling sprite at (%zu, %zu) by %.2f", spriteInfo.x,
                      spriteInfo.y, scale);
    applyTransformedSprite(
        spriteInfo,
        scaleSpriteImage(spriteInfo.currentSprite(), scale, activeFont()));
  }

  /// Rotates a sprite image and redraws it at its current position.
  void rotateSprite(SpriteInfo& spriteInfo, float angleDegrees) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Rotating sprite at (%zu, %zu) by %.2f degrees",
                      spriteInfo.x, spriteInfo.y, angleDegrees);
    applyTransformedSprite(
        spriteInfo, rotateSpriteImage(spriteInfo.currentSprite(), angleDegrees,
                                      spriteInfo.invisibleColor, activeFont()));
  }

  /// ISurface<RGB_T> interface delegation
  void setPixel(size_t x, size_t y, RGB_T color) override {
    surface_.setPixel(x, y, color);
  }
  /// Returns the pixel at the given position.
  RGB_T getPixel(size_t x, size_t y) const override {
    return surface_.getPixel(x, y);
  }
  /// Resizes the framebuffer surface.
  bool resize(size_t w, size_t h) override { return surface_.resize(w, h); }
  /// Returns the framebuffer width in pixels.
  size_t width() const override { return surface_.width(); }
  /// Returns the framebuffer height in pixels.
  size_t height() const override { return surface_.height(); }
  /// Sets the font for text rendering.
  void setFont(IFont<RGB_T>& font) { surface_.setFont(font); }
  /// Returns the currently set font for text rendering.
  IFont<RGB_T>& font() { return surface_.font(); }
  /// Clears the framebuffer with a single color.
  void clear(RGB_T color = RGB_T()) { surface_.clear(color); }
  /// Scrolls the framebuffer content by the specified offsets.
  void scroll(int dx, int dy) override { surface_.scroll(dx, dy); }
  /// Draws a line between two points.
  void drawLine(size_t x0, size_t y0, size_t x1, size_t y1, RGB_T color) {
    surface_.drawLine(x0, y0, x1, y1, color);
  }
  /// Draws a rectangle outline.
  void drawRect(size_t x, size_t y, size_t w, size_t h, RGB_T color) {
    surface_.drawRect(x, y, w, h, color);
  }
  /// Fills a rectangle.
  void fillRect(size_t x, size_t y, size_t w, size_t h, RGB_T color) {
    surface_.fillRect(x, y, w, h, color);
  }
  /// Draws a circle outline.
  void drawCircle(size_t x, size_t y, size_t r, RGB_T color) {
    surface_.drawCircle(x, y, r, color);
  }
  /// Fills a circle.
  void fillCircle(size_t x, size_t y, size_t r, RGB_T color) {
    surface_.fillCircle(x, y, r, color);
  }
  /// Draws a sprite.
  void drawSprite(size_t x, size_t y, const ISurface<RGB_T>& sprite,
                  RGB_T invisibleColor = RGB_T()) {
    surface_.drawSprite(x, y, sprite, invisibleColor);
  }
  /// Clears a sprite.
  void clearSprite(size_t x, size_t y, ISurface<RGB_T>& sprite,
                   RGB_T clearColor = RGB_T()) {
    surface_.clearSprite(x, y, sprite, clearColor);
  }
  /// Copies a sprite.
  void copySprite(size_t x, size_t y, const ISurface<RGB_T>& sprite) {
    surface_.copySprite(x, y, sprite);
  }
  /// Draws UTF-8 text.
  void drawText(int16_t x, int16_t y, const char* text, RGB_T foreground,
                RGB_T background = RGB_T(), bool opaque = false,
                uint8_t scale = 1, uint8_t spacing = 1,
                uint8_t lineSpacing = 1) {
    surface_.drawText(x, y, text, foreground, background, opaque, scale,
                      spacing, lineSpacing);
  }
  /// Returns the line printer for text rendering.
  LinePrinter<RGB_T>& linePrinter() { return surface_.linePrinter(); }
  /// Checks if the given coordinates are within the surface bounds.
  bool isInBounds(size_t x, size_t y) const {
    return surface_.isInBounds(x, y);
  }
  /// Sets a pixel with clipping.
  void setPixelClipped(size_t x, size_t y, RGB_T color) {
    surface_.setPixelClipped(x, y, color);
  }
  /// Draws a horizontal line with clipping.
  void drawHorizontalLineClipped(int x0, int x1, int y, RGB_T color) {
    surface_.drawHorizontalLineClipped(x0, x1, y, color);
  }

  /// Returns the raw pixel buffer as bytes.
  const uint8_t* data() const override { return surface_.data(); }
  /// Returns the size of the buffer in bytes.
  size_t size() const override { return surface_.size(); }

 protected:
  // Underlying surface for all drawing/storage
  SurfaceT surface_;

  struct Rect {
    size_t x = 0;
    size_t y = 0;
    size_t width = 0;
    size_t height = 0;
  };

  /// Returns the currently active font used by the framebuffer.
  IFont<RGB_T>& activeFont() { return surface_.font(); }
  Vector<std::unique_ptr<SpriteInfo>> sprites_;

  /// Returns the intersection rectangle of two rectangles.
  static Rect intersect(const Rect& first, const Rect& second) {
    const size_t overlapX = std::max(first.x, second.x);
    const size_t overlapY = std::max(first.y, second.y);
    const size_t firstRight = first.x + first.width;
    const size_t firstBottom = first.y + first.height;
    const size_t secondRight = second.x + second.width;
    const size_t secondBottom = second.y + second.height;
    const size_t overlapRight = std::min(firstRight, secondRight);
    const size_t overlapBottom = std::min(firstBottom, secondBottom);

    if (overlapX >= overlapRight || overlapY >= overlapBottom) {
      return {};
    }

    return {overlapX, overlapY, overlapRight - overlapX,
            overlapBottom - overlapY};
  }

  /// Returns true if the rectangle is empty (zero width or height).
  static bool isEmpty(const Rect& rect) {
    return rect.width == 0 || rect.height == 0;
  }

  /// Applies a transformed sprite image to the framebuffer and updates
  /// bookkeeping.
  void applyTransformedSprite(SpriteInfo& spriteInfo,
                              Surface<RGB_T>&& transformedSprite) {
    const Rect oldBounds{spriteInfo.x, spriteInfo.y,
                         spriteInfo.currentSprite().width(),
                         spriteInfo.currentSprite().height()};
    const size_t anchoredX = centeredCoordinate(spriteInfo.x, oldBounds.width,
                                                transformedSprite.width());
    const size_t anchoredY = centeredCoordinate(spriteInfo.y, oldBounds.height,
                                                transformedSprite.height());
    const Rect newBounds{anchoredX, anchoredY, transformedSprite.width(),
                         transformedSprite.height()};
    const Rect overlap = intersect(oldBounds, newBounds);

    restoreExposedPixels(spriteInfo, oldBounds, overlap);
    Surface<RGB_T> updatedBackground =
        captureUpdatedBackground(spriteInfo, oldBounds, newBounds, overlap);

    spriteInfo.x = anchoredX;
    spriteInfo.y = anchoredY;
    spriteInfo.setTransformedSprite(std::move(transformedSprite));
    spriteInfo.originalPixels = std::move(updatedBackground);
    this->drawSprite(spriteInfo.x, spriteInfo.y, spriteInfo.currentSprite(),
                     spriteInfo.invisibleColor);
  }

  /// Calculates the coordinate to center a new size over an old position/size.
  static size_t centeredCoordinate(size_t oldPosition, size_t oldSize,
                                   size_t newSize) {
    const float centeredPosition =
        static_cast<float>(oldPosition) +
        (static_cast<float>(oldSize) - static_cast<float>(newSize)) / 2.0;
    return centeredPosition <= 0.0
               ? 0
               : static_cast<size_t>(std::lround(centeredPosition));
  }

  /// Restores only the pixels exposed by moving a sprite, using the overlap
  /// region.
  void restoreExposedPixels(const SpriteInfo& spriteInfo, const Rect& oldBounds,
                            const Rect& overlap) {
    if (isEmpty(overlap)) {
      restoreOriginalPixels(spriteInfo);
      return;
    }

    drawSpriteRegion(spriteInfo.originalPixels, 0, 0, oldBounds.x, oldBounds.y,
                     oldBounds.width, overlap.y - oldBounds.y);

    const size_t overlapBottom = overlap.y + overlap.height;
    drawSpriteRegion(spriteInfo.originalPixels, 0, overlapBottom - oldBounds.y,
                     oldBounds.x, overlapBottom, oldBounds.width,
                     (oldBounds.y + oldBounds.height) - overlapBottom);

    drawSpriteRegion(spriteInfo.originalPixels, 0, overlap.y - oldBounds.y,
                     oldBounds.x, overlap.y, overlap.x - oldBounds.x,
                     overlap.height);

    const size_t overlapRight = overlap.x + overlap.width;
    drawSpriteRegion(spriteInfo.originalPixels, overlapRight - oldBounds.x,
                     overlap.y - oldBounds.y, overlapRight, overlap.y,
                     (oldBounds.x + oldBounds.width) - overlapRight,
                     overlap.height);
  }

  /// Captures the updated background pixels after a sprite move/transform.
  Surface<RGB_T> captureUpdatedBackground(const SpriteInfo& spriteInfo,
                                          const Rect& oldBounds,
                                          const Rect& newBounds,
                                          const Rect& overlap) {
    Surface<RGB_T> updatedBackground(newBounds.width, newBounds.height,
                                     activeFont());
    updatedBackground.begin();

    if (isEmpty(overlap)) {
      captureFramebufferRegion(updatedBackground, 0, 0, newBounds.x,
                               newBounds.y, newBounds.width, newBounds.height);
      return updatedBackground;
    }

    copySpriteRegion(spriteInfo.originalPixels, overlap.x - oldBounds.x,
                     overlap.y - oldBounds.y, updatedBackground,
                     overlap.x - newBounds.x, overlap.y - newBounds.y,
                     overlap.width, overlap.height);

    captureFramebufferRegion(updatedBackground, 0, 0, newBounds.x, newBounds.y,
                             newBounds.width, overlap.y - newBounds.y);

    const size_t overlapBottom = overlap.y + overlap.height;
    captureFramebufferRegion(updatedBackground, 0, overlapBottom - newBounds.y,
                             newBounds.x, overlapBottom, newBounds.width,
                             (newBounds.y + newBounds.height) - overlapBottom);

    captureFramebufferRegion(updatedBackground, 0, overlap.y - newBounds.y,
                             newBounds.x, overlap.y, overlap.x - newBounds.x,
                             overlap.height);

    const size_t overlapRight = overlap.x + overlap.width;
    captureFramebufferRegion(updatedBackground, overlapRight - newBounds.x,
                             overlap.y - newBounds.y, overlapRight, overlap.y,
                             (newBounds.x + newBounds.width) - overlapRight,
                             overlap.height);

    return updatedBackground;
  }

  /// Draws a rectangular region from a source surface onto the framebuffer.
  void drawSpriteRegion(const ISurface<RGB_T>& source, size_t sourceX,
                        size_t sourceY, size_t destX, size_t destY,
                        size_t width, size_t height) {
    for (size_t currentY = 0; currentY < height; ++currentY) {
      for (size_t currentX = 0; currentX < width; ++currentX) {
        const size_t framebufferX = destX + currentX;
        const size_t framebufferY = destY + currentY;
        if (framebufferX < surface_.width() &&
            framebufferY < surface_.height()) {
          surface_.setPixel(
              framebufferX, framebufferY,
              source.getPixel(sourceX + currentX, sourceY + currentY));
        }
      }
    }
  }

  /// Copies a rectangular region from a source surface to a destination
  /// surface.
  static void copySpriteRegion(const ISurface<RGB_T>& source, size_t sourceX,
                               size_t sourceY, Surface<RGB_T>& destination,
                               size_t destX, size_t destY, size_t width,
                               size_t height) {
    for (size_t currentY = 0; currentY < height; ++currentY) {
      for (size_t currentX = 0; currentX < width; ++currentX) {
        destination.setPixel(
            destX + currentX, destY + currentY,
            source.getPixel(sourceX + currentX, sourceY + currentY));
      }
    }
  }

  /// Captures a rectangular region from the framebuffer into a destination
  /// surface.
  void captureFramebufferRegion(Surface<RGB_T>& destination, size_t destX,
                                size_t destY, size_t sourceX, size_t sourceY,
                                size_t width, size_t height) {
    for (size_t currentY = 0; currentY < height; ++currentY) {
      for (size_t currentX = 0; currentX < width; ++currentX) {
        const size_t framebufferX = sourceX + currentX;
        const size_t framebufferY = sourceY + currentY;
        const RGB_T color =
            framebufferX < surface_.width() && framebufferY < surface_.height()
                ? surface_.getPixel(framebufferX, framebufferY)
                : RGB_T(0);
        destination.setPixel(destX + currentX, destY + currentY, color);
      }
    }
  }

  /// Returns a scaled copy of a sprite image.
  static Surface<RGB_T> scaleSpriteImage(const ISurface<RGB_T>& source,
                                         float scale, IFont<RGB_T>& font) {
    size_t scaledWidth = static_cast<size_t>(source.width() * scale);
    size_t scaledHeight = static_cast<size_t>(source.height() * scale);

    if (scaledWidth == 0) {
      scaledWidth = 1;
    }
    if (scaledHeight == 0) {
      scaledHeight = 1;
    }

    Surface<RGB_T> scaledSprite(scaledWidth, scaledHeight, font);
    scaledSprite.begin();
    for (size_t currentY = 0; currentY < scaledHeight; ++currentY) {
      const size_t sourceY =
          static_cast<size_t>(static_cast<float>(currentY) / scale);
      const size_t clampedY =
          sourceY < source.height() ? sourceY : source.height() - 1;
      for (size_t currentX = 0; currentX < scaledWidth; ++currentX) {
        const size_t sourceX =
            static_cast<size_t>(static_cast<float>(currentX) / scale);
        const size_t clampedX =
            sourceX < source.width() ? sourceX : source.width() - 1;
        scaledSprite.setPixel(currentX, currentY,
                              source.getPixel(clampedX, clampedY));
      }
    }

    return scaledSprite;
  }

  /// Returns a rotated copy of a sprite image, filling empty space with
  /// fillColor.
  static Surface<RGB_T> rotateSpriteImage(const ISurface<RGB_T>& source,
                                          float angleDegrees, RGB_T fillColor,
                                          IFont<RGB_T>& font) {
    const float radians =
        static_cast<float>(angleDegrees) * 3.14159265358979323846 / 180.0;
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const float sourceWidth = static_cast<float>(source.width());
    const float sourceHeight = static_cast<float>(source.height());
    size_t rotatedWidth = static_cast<size_t>(std::ceil(
        std::fabs(sourceWidth * cosine) + std::fabs(sourceHeight * sine)));
    size_t rotatedHeight = static_cast<size_t>(std::ceil(
        std::fabs(sourceWidth * sine) + std::fabs(sourceHeight * cosine)));

    if (rotatedWidth == 0) {
      rotatedWidth = 1;
    }
    if (rotatedHeight == 0) {
      rotatedHeight = 1;
    }

    Surface<RGB_T> rotatedSprite(rotatedWidth, rotatedHeight, font);
    rotatedSprite.begin();
    rotatedSprite.clear(fillColor);

    const float sourceCenterX = (sourceWidth - 1.0) / 2.0;
    const float sourceCenterY = (sourceHeight - 1.0) / 2.0;
    const float rotatedCenterX = (static_cast<float>(rotatedWidth) - 1.0) / 2.0;
    const float rotatedCenterY =
        (static_cast<float>(rotatedHeight) - 1.0) / 2.0;

    for (size_t currentY = 0; currentY < rotatedHeight; ++currentY) {
      for (size_t currentX = 0; currentX < rotatedWidth; ++currentX) {
        const float targetX = static_cast<float>(currentX) - rotatedCenterX;
        const float targetY = static_cast<float>(currentY) - rotatedCenterY;
        const float sourceX =
            (targetX * cosine) + (targetY * sine) + sourceCenterX;
        const float sourceY =
            (-targetX * sine) + (targetY * cosine) + sourceCenterY;
        const long nearestX = std::lround(sourceX);
        const long nearestY = std::lround(sourceY);

        if (nearestX >= 0 && nearestY >= 0 &&
            static_cast<size_t>(nearestX) < source.width() &&
            static_cast<size_t>(nearestY) < source.height()) {
          rotatedSprite.setPixel(
              currentX, currentY,
              source.getPixel(static_cast<size_t>(nearestX),
                              static_cast<size_t>(nearestY)));
        }
      }
    }

    return rotatedSprite;
  }

  /// Restores the original background pixels behind a sprite.
  void restoreOriginalPixels(const SpriteInfo& spriteInfo) {
    surface_.drawSprite(spriteInfo.x, spriteInfo.y, spriteInfo.originalPixels);
  }

  /// Finds the iterator to a sprite in the internal sprite list.
  Vector<std::unique_ptr<SpriteInfo>>::iterator findSprite(
      SpriteInfo& spriteInfo) {
    return std::find_if(
        sprites_.begin(), sprites_.end(),
        [&spriteInfo](const std::unique_ptr<SpriteInfo>& entry) {
          return entry.get() == &spriteInfo;
        });
  }
};

}  // namespace tinygpu