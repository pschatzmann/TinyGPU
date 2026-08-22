
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include "TinyGPU/Surface/Surface.h"
#include "TinyGPU/ThreeD/Vector.h"
#include "TinyGPUConfig.h"
#include "TinyGPU/Util/TinyGPULogger.h"
#include "TinyGPU/Input/TouchDriver.h"
#include "TinyGPU/Surface/SpriteInfo.h"

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
  /// Creates an empty framebuffer.
  FrameBuffer() = default;

  /// Creates a framebuffer with the specified size and font.
  FrameBuffer(size_t width, size_t height, IFont<RGB_T>& font)
      : surface_(width, height, font) {}

  bool begin() override { return surface_.begin(); }
  void end() override { surface_.end(); }

  void setTouchDriver(TouchDriver& touch) { p_touchDriver = &touch; }

  /// Adds a sprite to the framebuffer and draws it at the given position.
  SpriteInfo<RGB_T, SurfaceT>& addSprite(size_t x, size_t y, const ISurface<RGB_T>& sprite,
                        RGB_T invisibleColor = RGB_T(0)) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO, "Adding sprite at (%zu, %zu)",
                      x, y);
    auto info = std::make_unique<SpriteInfo<RGB_T, SurfaceT>>(x, y, sprite, invisibleColor,
                                             activeFont());

    info->saveOriginalPixels(surface_);
    surface_.drawSprite(x, y, info->currentSprite(), invisibleColor);

    sprites_.push_back(std::move(info));
    return *sprites_.back();
  }

  /// Adds a sprite with a preallocated max buffer size for transformations.
  SpriteInfo<RGB_T, SurfaceT>& addSprite(size_t x, size_t y, size_t maxX, size_t maxY,
                        const ISurface<RGB_T>& sprite,
                        RGB_T invisibleColor = RGB_T(0)) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Adding sprite at (%zu, %zu) with max size (%zu, %zu)", x,
                      y, maxX, maxY);
    auto info = std::make_unique<SpriteInfo<RGB_T, SurfaceT>>(x, y, sprite, invisibleColor,
                                             activeFont());
    info->setMaxSize(maxX, maxY);
    info->saveOriginalPixels(surface_);
    surface_.drawSprite(x, y, info->currentSprite(), invisibleColor);
    sprites_.push_back(std::move(info));
    return *sprites_.back();
  }

  /// Removes a sprite and restores the pixels behind it.
  void removeSprite(SpriteInfo<RGB_T, SurfaceT>& spriteInfo) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO, "Removing sprite at (%zu, %zu)",
                      spriteInfo.x, spriteInfo.y);
    restoreOriginalPixels(spriteInfo);
    auto it = tinygpu::findSprite(sprites_, spriteInfo);
    if (it != sprites_.end()) {
      sprites_.erase(it);
    }
  }

  /// Returns a pointer to the sprite at the given position, if any.
  SpriteInfo<RGB_T, SurfaceT>* getSprite(size_t x, size_t y) {
    for (auto& sprite : sprites_) {
      if (x >= sprite->x && x < sprite->x + sprite->currentSprite().width() &&
          y >= sprite->y && y < sprite->y + sprite->currentSprite().height()) {
        return sprite.get();
      }
    }
    return nullptr;
  }


  /// Returns the number of sprites currently managed by the framebuffer.
  size_t getSpriteCount() const { return sprites_.size(); }

  /// Returns a pointer to the sprite at the given index, if valid.
  SpriteInfo<RGB_T, SurfaceT>* getSprite(int idx) {
    if (idx < 0 || idx >= static_cast<int>(sprites_.size())) {
      return nullptr;
    }
    return sprites_[idx].get();
  }

  SpriteInfo<RGB_T, SurfaceT>* getSpritePtr(int idx) {
    if (idx < 0 || idx >= static_cast<int>(sprites_.size())) {
      return nullptr;
    }
    return sprites_[idx].get();;
  }

  /// Moves a sprite to a new position and redraws it.
  void moveSprite(SpriteInfo<RGB_T, SurfaceT>& spriteInfo, size_t newX, size_t newY) {
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
    const Rect overlap = Rect::intersect(oldBounds, newBounds);

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
  void scaleSprite(SpriteInfo<RGB_T, SurfaceT>& spriteInfo, float scale) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Scaling sprite at (%zu, %zu) by %.2f", spriteInfo.x,
                      spriteInfo.y, scale);
    applyTransformedSprite(
        spriteInfo,
        scaleSpriteImage(spriteInfo.currentSprite(), scale, activeFont()));
  }

  /// Rotates a sprite image and redraws it at its current position.
  void rotateSprite(SpriteInfo<RGB_T, SurfaceT>& spriteInfo, float angleDegrees) {
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

  /// Sets the framebuffer pixel data directly, replacing the current content.
  bool setData(uint8_t* data, size_t dataSize) {
    memset(const_cast<uint8_t*>(surface_.data()), 0, surface_.size());
    if (dataSize > surface_.size()) {
      return false;
    }
    std::memcpy(const_cast<uint8_t*>(surface_.data()), data, dataSize);
  }

  /// checks if the given coordinates are within the surface bounds.
  bool contains(size_t x, size_t y) override { return surface_.contains(x, y); }

  /// Processes touch input and dispatches events to sprites if touched.
  void processTouch() {
    if (p_touchDriver && p_touchDriver->isTouched()) {
      Point point;
      if (p_touchDriver->getPoint(point)) {
        TinyGPULogger.log(TinyGPULoggerClass::INFO,
                          "Touch detected at (%d, %d) with pressure %d",
                          point.x, point.y, point.pressure);
        auto* sprite = getSprite(point.x, point.y);
        if (sprite) {
          TinyGPULogger.log(TinyGPULoggerClass::INFO,
                            "Touch is within sprite at (%zu, %zu)", sprite->x,
                            sprite->y);
          // Call the sprite's onTouch callback if set
          sprite->onTouch(point);
        }
      }
    }
  }

 protected:
  // Underlying surface for all drawing/storage
  SurfaceT surface_;
  TouchDriver* p_touchDriver = nullptr;

  /// Returns the currently active font used by the framebuffer.
  IFont<RGB_T>& activeFont() { return surface_.font(); }
  Vector<std::unique_ptr<SpriteInfo<RGB_T, SurfaceT>>> sprites_;

  /// Returns true if the rectangle is empty (zero width or height).
  static bool isEmpty(const Rect& rect) {
    return rect.width == 0 || rect.height == 0;
  }

  /// Applies a transformed sprite image to the framebuffer and updates
  /// bookkeeping.
  void applyTransformedSprite(SpriteInfo<RGB_T, SurfaceT>& spriteInfo,
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
    const Rect overlap = Rect::intersect(oldBounds, newBounds);

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


  /// Calculates the coordinate to center a new size over an old
  /// position/size.
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
  void restoreExposedPixels(const SpriteInfo<RGB_T, SurfaceT>& spriteInfo, const Rect& oldBounds,
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
  Surface<RGB_T> captureUpdatedBackground(const SpriteInfo<RGB_T, SurfaceT>& spriteInfo,
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

  /// Restores the original background pixels behind a sprite.
  void restoreOriginalPixels(const SpriteInfo<RGB_T, SurfaceT>& spriteInfo) {
    surface_.drawSprite(spriteInfo.x, spriteInfo.y, spriteInfo.originalPixels);
  }
};

}  // namespace tinygpu