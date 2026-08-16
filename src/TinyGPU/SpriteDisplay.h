#pragma once
#include <algorithm>
#include <memory>

#include "RGB565.h"
#include "SpriteInfo.h"
#include "Surface.h"
#include "TinyGPU/Vector.h"
#include "TinyGPUConfig.h"
#include "TinyGPULogger.h"
#include "TouchDriver.h"

namespace tinygpu {

/***
 * @brief Sprite Display without FrameBuffer. Usually we use a Framebuffer to
 * compose and display sprites, but in some cases we want to display a sprite
 * directly on the screen without using a framebuffer to minimize the required
 * memory.
 * The Sprites are also supporting touch events and can be moved, scaled and
 * rotated.
 * @tparam RGB_T The pixel format, default is RGB565.
 */

template <typename RGB_T = RGB565>
class SpriteDisplay {
 public:
  SpriteDisplay(size_t width, size_t height, DisplayDriver<RGB_T>& driver,
                RGB_T backgroundColor = RGB_T(0))
      : width_(width),
        height_(height),
        driver_(driver),
        backgroundColor_(backgroundColor) {}

  /// Adds a sprite to the framebuffer and draws it at the given position.
  SpriteInfo<RGB_T, Surface<RGB_T>>& addSprite(size_t x, size_t y,
                                               const ISurface<RGB_T>& sprite) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO, "Adding sprite at (%zu, %zu)",
                      x, y);
    auto info = std::make_unique<SpriteInfo<RGB_T, Surface<RGB_T>>>(
        x, y, sprite, backgroundColor_, defaultFont_);

    displaySprite(x, y, info->currentSprite());

    sprites_.push_back(std::move(info));
    return *sprites_.back();
  }

  /// Starts the processing
  bool begin() {
    bool ok = driver_.begin();
    clear();
    return ok;
  }

  /// Ends the processing
  void end() {
    driver_.end();
    clear();
  }

  /// Adds a sprite with a preallocated max buffer size for transformations.
  SpriteInfo<RGB_T, Surface<RGB_T>>& addSprite(size_t x, size_t y, size_t maxX,
                                               size_t maxY,
                                               RGB_T backgroundColor) {
    auto sprite = std::make_unique<Sprite<RGB_T>>(maxX, maxY, defaultFont_);
    sprite->begin();
    sprite->clear(backgroundColor);

    // Pass color during addSprite or rely on constructor initialization
    SpriteInfo<RGB_T, Surface<RGB_T>>& result = addSprite(x, y, *sprite);
    result.isSurfaceAutoDelete = true;
    // Ownership of the Sprite now belongs to the SpriteInfo (deleted via
    // isSurfaceAutoDelete), so release it here to avoid a double free.
    sprite.release();
    return result;
  }

  /// Adds a sprite with a preallocated max buffer size for transformations.
  SpriteInfo<RGB_T, Surface<RGB_T>>& addSprite(size_t x, size_t y, size_t maxX,
                                               size_t maxY,
                                               const ISurface<RGB_T>& sprite) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Adding sprite at (%zu, %zu) with max size (%zu, %zu)", x,
                      y, maxX, maxY);
    auto info =
        std::make_unique<SpriteInfoWithoutBackground<RGB_T, Surface<RGB_T>>>(
            x, y, sprite, backgroundColor_, defaultFont_);
    info->setMaxSize(maxX, maxY);
    displaySprite(x, y, info->currentSprite());
    sprites_.push_back(std::move(info));
    return *sprites_.back();
  }

  /// Removes a sprite and restores the pixels behind it.
  void removeSprite(SpriteInfo<RGB_T, Surface<RGB_T>>& sprite) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO, "Removing sprite at (%zu, %zu)",
                      sprite.x, sprite.y);

    auto it = tinygpu::findSprite(sprites_, sprite);
    if (it != sprites_.end()) {
      clearSprite(sprite.x, sprite.y, sprite.currentSprite());
      sprites_.erase(it);
    }
  }

  /// Returns a reference to the sprite at the given position, if any.
  SpriteInfo<RGB_T, Surface<RGB_T>>* getSprite(size_t x, size_t y) {
    for (const auto& sprite : sprites_) {
      if (x >= sprite->x && x < sprite->x + sprite->currentSprite().width() &&
          y >= sprite->y && y < sprite->y + sprite->currentSprite().height()) {
        return sprite.get();
      }
    }
    return nullptr;
  }

  /// Returns the number of sprites currently managed by the framebuffer.
  size_t getSpriteCount() const { return sprites_.size(); }

  /// Returns a reference to the sprite at the given index, if valid.
  SpriteInfo<RGB_T, Surface<RGB_T>>* getSprite(int idx) {
    if (idx < 0 || idx >= static_cast<int>(sprites_.size())) {
      return nullptr;
    }
    return sprites_[idx].get();
  }

  /// Returns a reference to the SpriteInfo for the indicated surface, if it
  /// exists.
  SpriteInfo<RGB_T, Surface<RGB_T>>* getSprite(const ISurface<RGB_T>& sprite) {
    for (auto& spriteInfo : sprites_) {
      if (spriteInfo->currentSprite().data() == sprite.data()) {
        return spriteInfo.get();
      }
    }
    return nullptr;
  }

  /// Moves a sprite to a new position and redraws it.
  void moveSprite(SpriteInfo<RGB_T, Surface<RGB_T>>& sprite, size_t newX,
                  size_t newY) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Moving sprite from (%zu, %zu) to (%zu, %zu)", sprite.x,
                      sprite.y, newX, newY);
    if (sprite.x == newX && sprite.y == newY) {
      return;
    }

    if (is_clear_on_sprite_move) {
      clearSprite(sprite.x, sprite.y, sprite.currentSprite());
    }

    sprite.x = newX;
    sprite.y = newY;
    displaySprite(sprite.x, sprite.y, sprite.currentSprite());
  }

  /// Scales a sprite image and redraws it at its current position.
  void scaleSprite(SpriteInfo<RGB_T, Surface<RGB_T>>& sprite, float scale) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Scaling sprite at (%zu, %zu) by %.2f", sprite.x,
                      sprite.y, scale);
    applyTransformedSprite(
        sprite, scaleSpriteImage(sprite.currentSprite(), scale, defaultFont_));
  }

  /// Rotates a sprite image and redraws it at its current position.
  void rotateSprite(SpriteInfo<RGB_T, Surface<RGB_T>>& sprite,
                    float angleDegrees) {
    TinyGPULogger.log(TinyGPULoggerClass::INFO,
                      "Rotating sprite at (%zu, %zu) by %.2f degrees", sprite.x,
                      sprite.y, angleDegrees);
    applyTransformedSprite(
        sprite, rotateSpriteImage(sprite.currentSprite(), angleDegrees,
                                  sprite.invisibleColor, defaultFont_));
  }

  /// Clears the display and resets the background color.
  void clear() {
    TinyGPULogger.log(TinyGPULoggerClass::INFO, "Clearing display");
    fillScreen(backgroundColor_);
    sprites_.clear();
  }

  /// Refreshes the display and redraws all sprites in their current positions
  /// and states.
  void refresh() {
    fillScreen(backgroundColor_);
    for (const auto& sprite : sprites_) {
      displaySprite(sprite->x, sprite->y, sprite->currentSprite());
    }
  }

  /// Defines the touch driver
  void setTouchDriver(TouchDriver& touch) { p_touchDriver = &touch; }

  /// Processes touch input and dispatches events to sprites if touched.
  void processTouch() {
    if (p_touchDriver && p_touchDriver->isTouched()) {
      Point point;
      if (p_touchDriver->getPoint(point)) {
        TinyGPULogger.log(TinyGPULoggerClass::INFO,
                          "Touch detected at (%d, %d) with pressure %d",
                          point.x, point.y, point.pressure);
        SpriteInfo<RGB_T, Surface<RGB_T>>* sprite = getSprite(point.x, point.y);
        if (sprite) {
          TinyGPULogger.log(TinyGPULoggerClass::INFO,
                            "Touch is within sprite at (%zu, %zu)", sprite->x,
                            sprite->y);
          sprite->onTouch(point);
        }
      }
    }
  }

  /// Fills/clears the entire display with the specified color.
  void fillScreen(RGB_T color) { driver_.writeColor(width_, height_, color); }

  /// Activates/deactivates the automatic clearing when we move a sprite
  void setClearOnSpriteMove(bool clear) { is_clear_on_sprite_move = clear; }

  /// Returns whether the automatic clearing is active when we move a sprite
  bool isClearOnSpriteMove() const { return is_clear_on_sprite_move; }

 protected:
  DisplayDriver<RGB_T>& driver_;
  TouchDriver* p_touchDriver = nullptr;
  Sprite<RGB_T> actual_sprite_background_;
  size_t width_;
  size_t height_;
  RGB_T backgroundColor_;
  Vector<std::unique_ptr<SpriteInfo<RGB_T, Surface<RGB_T>>>> sprites_;
  BitmapFont<RGB_T> defaultFont_;
  bool is_clear_on_sprite_move = true;

  /// Helper to calculate centered coordinate when scaling/rotating
  static size_t centeredCoordinate(size_t pos, size_t oldDim, size_t newDim) {
    if (newDim >= oldDim) {
      size_t diff = (newDim - oldDim) / 2;
      return (pos > diff) ? (pos - diff) : 0;
    } else {
      return pos + (oldDim - newDim) / 2;
    }
  }

  /// Applies a transformed sprite image to the display and updates bookkeeping.
  void applyTransformedSprite(SpriteInfo<RGB_T, Surface<RGB_T>>& spriteInfo,
                              Surface<RGB_T>&& transformedSprite) {
    const Rect oldBounds{spriteInfo.x, spriteInfo.y,
                         spriteInfo.currentSprite().width(),
                         spriteInfo.currentSprite().height()};
    const size_t anchoredX = centeredCoordinate(spriteInfo.x, oldBounds.width,
                                                transformedSprite.width());
    const size_t anchoredY = centeredCoordinate(spriteInfo.y, oldBounds.height,
                                                transformedSprite.height());

    clearSprite(spriteInfo.x, spriteInfo.y, oldBounds.width, oldBounds.height);

    spriteInfo.x = anchoredX;
    spriteInfo.y = anchoredY;
    spriteInfo.setTransformedSprite(std::move(transformedSprite));
    displaySprite(spriteInfo.x, spriteInfo.y, spriteInfo.currentSprite());
  }

  /// Clears the background of the indicated sprite at (x, y) with the
  /// background color.
  void clearSprite(size_t x, size_t y, const ISurface<RGB_T>& sprite) {
    clearSprite(x, y, sprite.width(), sprite.height());
  }

  void clearSprite(Rect rect) {
    clearSprite(rect.x, rect.y, rect.width, rect.height);
  }

  void clearSprite(size_t x, size_t y, size_t width, size_t height) {
    actual_sprite_background_.resize(width, height);
    actual_sprite_background_.clear(backgroundColor_);

    // Route via writeData instead of drawSurface
    driver_.writeData(actual_sprite_background_, x, y);
  }
  /// Draws a sprite at (x, y)
  void displaySprite(size_t x, size_t y, const ISurface<RGB_T>& sprite) {
    // Route via writeData instead of drawSurface
    driver_.writeData(const_cast<ISurface<RGB_T>&>(sprite), x, y);
  }
};

}  // namespace tinygpu