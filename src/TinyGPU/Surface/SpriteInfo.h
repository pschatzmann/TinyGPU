#pragma once
#include <algorithm>
#include <cmath>
#include <memory>

#include "TinyGPU/Color/RGB565.h"
#include "TinyGPU/ThreeD/Vector.h"

namespace tinygpu {

/**
 * @brief Represents a rectangular area on the screen or surface.
 *
 * The Rect structure is used to define a rectangle by its top-left corner
 * coordinates (x, y) and its dimensions (width, height). It is commonly used
 * for sprite placement, collision detection, and drawing operations.
 */
struct Rect {
  size_t x = 0;
  size_t y = 0;
  size_t width = 0;
  size_t height = 0;

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
};

/**
 * Tracks a sprite instance together with its saved background pixels.
 *
 * Each SpriteInfo stores the sprite position, transparent color, current
 * sprite image, and a snapshot of the framebuffer region covered by it.
 */
template <typename RGB_T = RGB565, typename SurfaceT = Surface<RGB_T>>
struct SpriteInfo {
  size_t x = 0;
  size_t y = 0;
  size_t maxWidth = 0;
  size_t maxHeight = 0;
  RGB_T invisibleColor = RGB_T(0);
  const ISurface<RGB_T>* sprite = nullptr;
  bool isSurfaceAutoDelete = false;
  std::unique_ptr<SurfaceT> transformedSprite;
  SurfaceT originalPixels;
  IFont<RGB_T>& fontRef;
  void (*onTouchCallback)(SpriteInfo&, Point) = nullptr;

  SpriteInfo(size_t startX, size_t startY, const ISurface<RGB_T>& sourceSprite,
             RGB_T transparentColor, IFont<RGB_T>& font)
      : x(startX),
        y(startY),
        invisibleColor(transparentColor),
        sprite(&sourceSprite),
        originalPixels(sourceSprite.width(), sourceSprite.height(), font),
        fontRef(font) {}

  virtual ~SpriteInfo() {
    if (isSurfaceAutoDelete && sprite) {
      delete sprite;
    }
  }

  Rect getRect() const {
    return {x, y, currentSprite().width(), currentSprite().height()};
  }

  /// Returns the sprite image currently used for drawing.
  const ISurface<RGB_T>& currentSprite() const {
    return transformedSprite
               ? static_cast<const ISurface<RGB_T>&>(*transformedSprite)
               : *sprite;
  }

  /// Saves the background pixels currently covered by the sprite.
  virtual void saveOriginalPixels(ISurface<RGB_T>& framebuffer) {
    originalPixels.resize(currentSprite().width(), currentSprite().height());
    framebuffer.copySprite(x, y, originalPixels);
  }

  void onTouch(Point point) {
    if (onTouchCallback) {
      onTouchCallback(*this, point);
    }
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

/**
 * A specialized SpriteInfo that does not save the background pixels.
 *
 * This is useful for sprites that do not need to restore the background,
 * such as those that are always drawn on a solid color or when the
 * background is managed separately.
 */
template <typename RGB_T = RGB565, typename SurfaceT = Surface<RGB_T>>
struct SpriteInfoWithoutBackground : public SpriteInfo<RGB_T, SurfaceT> {
  SpriteInfoWithoutBackground(size_t startX, size_t startY,
                              const ISurface<RGB_T>& sourceSprite,
                              RGB_T transparentColor, IFont<RGB_T>& font)
      : SpriteInfo<RGB_T, SurfaceT>(startX, startY, sourceSprite,
                                    transparentColor, font) {}

  // Do nothing; no background saving for this sprite type
  void saveOriginalPixels(ISurface<RGB_T>&) override {}
};

/// Finds the iterator to a sprite within a sprite list, shared by
/// FrameBuffer and SpriteDisplay.
template <typename RGB_T, typename SurfaceT>
inline typename Vector<std::unique_ptr<SpriteInfo<RGB_T, SurfaceT>>>::iterator
findSprite(Vector<std::unique_ptr<SpriteInfo<RGB_T, SurfaceT>>>& sprites,
          SpriteInfo<RGB_T, SurfaceT>& spriteInfo) {
  return std::find_if(
      sprites.begin(), sprites.end(),
      [&spriteInfo](const std::unique_ptr<SpriteInfo<RGB_T, SurfaceT>>& entry) {
        return entry.get() == &spriteInfo;
      });
}

/// Returns a scaled copy of a sprite image, shared by FrameBuffer and
/// SpriteDisplay.
template <typename RGB_T>
inline Surface<RGB_T> scaleSpriteImage(const ISurface<RGB_T>& source,
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
/// fillColor. Shared by FrameBuffer and SpriteDisplay.
template <typename RGB_T>
inline Surface<RGB_T> rotateSpriteImage(const ISurface<RGB_T>& source,
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

}  // namespace tinygpu