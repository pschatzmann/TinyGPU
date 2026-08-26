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
  // True once transformedSprite actually holds a scaled/rotated image (set by
  // setTransformedSprite()) rather than just the blank buffer setMaxSize()
  // preallocates - currentSprite() must not treat that blank buffer as "the"
  // sprite image, or the original sprite never gets drawn until the first
  // scaleSprite()/rotateSprite() call.
  bool hasTransformedContent = false;
  // Last scale/rotation requested via scaleSprite()/rotateSprite(). Both
  // are always re-rendered from the pristine `sprite` image using these
  // two values together (see renderTransformedSprite() below) - never by
  // re-scaling/re-rotating the already-transformed `transformedSprite`
  // raster, which would compound nearest-neighbor resampling artifacts
  // (no interpolation) frame after frame until the image degenerates into
  // a solid blob within seconds.
  float currentScale = 1.0f;
  float currentAngleDegrees = 0.0f;
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

  /// Returns the size a transformed image of (rawWidth, rawHeight) will
  /// actually end up stored/drawn at by setTransformedSprite() below. Once
  /// a fixed buffer exists (from setMaxSize() or an earlier
  /// setTransformedSprite() call), the drawn size is ALWAYS exactly
  /// maxWidth x maxHeight - never the raw image's own size - because
  /// setTransformedSprite() pads a smaller image with invisibleColor
  /// rather than shrinking the buffer to fit it (e.g. scaling down) and
  /// crops a larger one down to the buffer (e.g. rotating a square grows
  /// its bounding box). Only when no fixed buffer exists yet does the
  /// drawn size match the raw image exactly (setTransformedSprite()
  /// allocates the very first buffer to fit it precisely).
  ///
  /// Callers computing where to anchor/redraw a transformed sprite (see
  /// FrameBuffer::applyTransformedSprite(), SpriteDisplay::
  /// applyTransformedSprite()) must use this size, not the raw transformed
  /// image's own size, or their background restore/capture bounds won't
  /// match the region setTransformedSprite() + drawSprite() actually
  /// touch - leaving stale, un-restored pixels on screen (visible as a
  /// second, "ghost" copy of the sprite trailing the real one).
  Rect clampedSize(size_t rawWidth, size_t rawHeight) const {
    if (transformedSprite) {
      return {0, 0, maxWidth, maxHeight};
    }
    return {0, 0, rawWidth, rawHeight};
  }

  /// Returns the sprite image currently used for drawing.
  const ISurface<RGB_T>& currentSprite() const {
    return hasTransformedContent
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
    // Only copy the region that fits, centered in both the source and the
    // destination buffer - a top-left crop/pad would visually shift the
    // sprite toward the top-left corner whenever newSprite is larger or
    // smaller than the fixed buffer (e.g. a rotated image's larger
    // bounding box), which would then disagree with the centered anchor
    // position applyTransformedSprite() computed for it via
    // SpriteInfo::clampedSize() above.
    const size_t copyWidth = std::min(maxWidth, newSprite.width());
    const size_t copyHeight = std::min(maxHeight, newSprite.height());
    const size_t srcOffsetX = (newSprite.width() - copyWidth) / 2;
    const size_t srcOffsetY = (newSprite.height() - copyHeight) / 2;
    const size_t dstOffsetX = (maxWidth - copyWidth) / 2;
    const size_t dstOffsetY = (maxHeight - copyHeight) / 2;

    transformedSprite->clear(RGB_T(0));
    for (size_t y = 0; y < copyHeight; ++y) {
      for (size_t x = 0; x < copyWidth; ++x) {
        transformedSprite->setPixel(
            dstOffsetX + x, dstOffsetY + y,
            newSprite.getPixel(srcOffsetX + x, srcOffsetY + y));
      }
    }
    hasTransformedContent = true;
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

/// Renders spriteInfo's pristine `sprite` image at its currently requested
/// scale and rotation (SpriteInfo::currentScale/currentAngleDegrees, set by
/// scaleSprite()/rotateSprite() - see FrameBuffer.h/SpriteDisplay.h) in one
/// combined pass, shared by FrameBuffer and SpriteDisplay.
///
/// This always starts over from the pristine original, never from
/// spriteInfo.currentSprite() (the previous frame's already-transformed
/// raster) - scaleSpriteImage()/rotateSpriteImage() use nearest-neighbor
/// sampling with no interpolation, so repeatedly transforming an
/// already-transformed raster compounds resampling artifacts frame after
/// frame until the image degenerates into a solid blob within seconds
/// (confirmed empirically: an 18x18 circle sprite scaled/rotated every
/// frame that way fills its entire 40x40 max buffer within ~10 frames).
/// Re-deriving from the pristine source every time is also the only way to
/// support an absolute scale/angle contract at all - scaleSprite(info, 1.0)
/// must be able to undo a previous scaleSprite(info, 1.3), which is
/// impossible if 1.3x has already been baked irreversibly into the stored
/// raster.
template <typename RGB_T, typename SurfaceT>
inline Surface<RGB_T> renderTransformedSprite(
    const SpriteInfo<RGB_T, SurfaceT>& spriteInfo, IFont<RGB_T>& font) {
  Surface<RGB_T> scaled =
      scaleSpriteImage(*spriteInfo.sprite, spriteInfo.currentScale, font);
  return rotateSpriteImage(scaled, spriteInfo.currentAngleDegrees,
                           spriteInfo.invisibleColor, font);
}

}  // namespace tinygpu