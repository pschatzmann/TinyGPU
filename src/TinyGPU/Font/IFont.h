#pragma once
#include <stddef.h>
#include <stdint.h>
#include "RGB565.h"

namespace tinygpu {
// Forward declaration to avoid circular dependency
template <typename RGB_T> class ISurface;

/**
 * @brief Font rendering interface for TinyGPU-compatible framebuffers.
 *
 * @tparam RGB_T The pixel color type. Can be RGB565, RGB666, RGB888, etc.
 *
 * Implementations render text onto an ISurface target using their own glyph
 * storage, layout, and scaling behavior.
 */
template <typename RGB_T = RGB565>
class IFont {
 public:
  /// Destroys the font interface.
  virtual ~IFont() = default;

  /// Draws a text string onto a framebuffer target.
  virtual void drawText(ISurface<RGB_T>& target, int16_t x, int16_t y,
                        const char* text, RGB_T foreground, RGB_T background,
                        bool opaque = false, uint8_t scale = 1,
                        uint8_t spacing = 1, uint8_t lineSpacing = 1) const = 0;

  /// Returns the width of the longest text line in pixels.
  virtual size_t measureTextWidth(const char* text, uint8_t scale = 1,
                                  uint8_t spacing = 1) const = 0;

  /// Returns the total text height in pixels.
  virtual size_t measureTextHeight(const char* text, uint8_t scale = 1,
                                   uint8_t lineSpacing = 1) const = 0;

  /// Returns the scaled glyph height in pixels.
  virtual size_t getHeight(uint8_t scale) const = 0;
};

}  // namespace tinygpu