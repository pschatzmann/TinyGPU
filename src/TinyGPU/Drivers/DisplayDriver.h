#pragma once
#include <stddef.h>

#include "TinyGPU/Surface/ISurface.h"

namespace tinygpu {

/**
 * @brief Abstract base class for display drivers.
 *
 * Defines the interface for initializing the display, setting the address
 * window, and writing pixel data from a Surface. Specific display drivers (e.g.
 * SPI, SDL) should inherit from this and implement the virtual methods.
 */

/// Common rotation values for rectangular display drivers (row/column
/// exchange plus optional axis mirroring) - shared here, rather than
/// duplicated as a nested enum per concrete driver (as ILI9341Driver/
/// ILI9342Driver used to each define their own, identical, copy), so
/// generic code holding only a DisplayDriver<RGB_T>& (e.g. an LCDBoard's
/// display()) can call setRotation() without knowing the concrete driver
/// type. Concrete drivers keep a `using Rotation = tinygpu::DisplayRotation;`
/// alias so existing code written against e.g. ILI9341Driver<RGB565>::
/// Rotation still compiles unchanged. Named DisplayRotation (not just
/// Rotation) to avoid colliding with the differently-shaped touch
/// tinygpu::Rotation (Deg0/Deg90/Deg180/Deg270) in TouchDriver.h.
enum class DisplayRotation {
  kNone = -1,
  kPortrait = 0,
  kLandscape = 1,
  kPortraitFlipped = 2,
  kLandscapeFlipped = 3,
};

/// True for the landscape family (kLandscape/kLandscapeFlipped) - false
/// for portrait (kPortrait/kPortraitFlipped) or kNone. The landscape
/// family has its row/column addressing exchanged relative to portrait
/// (MV bit, on MIPI-DCS-style controllers), which is what makes
/// width()/height() swap between the two families - a same-family
/// rotation (e.g. kLandscape to kLandscapeFlipped, a 180 degree flip)
/// only changes scan direction, not addressable width/height.
inline bool isLandscapeFamily(DisplayRotation r) {
  return r == DisplayRotation::kLandscape ||
         r == DisplayRotation::kLandscapeFlipped;
}

template <typename RGB_T = RGB565>
class DisplayDriver {
 public:
  virtual ~DisplayDriver() = default;
  virtual bool begin() = 0;
  virtual void end() {}
  virtual bool writeData(ISurface<RGB_T>& surface) = 0;
  virtual bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) = 0;

  /// Changes the panel's rotation after begin(). Default no-op, for
  /// drivers with no such concept (e.g. a fixed-orientation panel, or a
  /// desktop/SDL backend) - concrete SPI drivers (ILI9341Driver,
  /// ILI9342Driver) override this to reprogram MADCTL.
  virtual void setRotation(DisplayRotation rotation) {}

  /// Current addressable panel width/height in pixels - for a driver
  /// that supports setRotation(), these reflect the *current* rotation
  /// (landscape vs portrait swaps them; see isLandscapeFamily()), not
  /// necessarily the panel's native/physical orientation. Callers that
  /// need to lay out content (e.g. OutputTinyGPU's scale_to_fit target,
  /// or clearScreen()'s band loop) should read these fresh rather than
  /// caching their own copy, so a setRotation() call is picked up
  /// automatically without needing separate bookkeeping.
  virtual size_t width() const = 0;
  virtual size_t height() const = 0;

  void writeColor(size_t width, size_t height, RGB_T color) {
    Sprite<RGB_T> tempSurface(width, 1, defaultFont);
    tempSurface.begin();
    for (int i = 0; i < width; ++i) tempSurface.setPixel(i, 0, color);

    for (int j = 0; j < height; ++j) {
      writeData(tempSurface, 0, j);
    }
  }

 protected:
  virtual bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) = 0;
  BitmapFont<RGB_T> defaultFont;
};

}  // namespace tinygpu