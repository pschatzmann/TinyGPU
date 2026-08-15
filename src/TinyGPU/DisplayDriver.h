#pragma once
#include <stddef.h>

#include "ISurface.h"

namespace tinygpu {

/**
 * @brief Abstract base class for display drivers.
 *
 * Defines the interface for initializing the display, setting the address
 * window, and writing pixel data from a Surface. Specific display drivers (e.g.
 * SPI, SDL) should inherit from this and implement the virtual methods.
 */

template <typename RGB_T = RGB565>
class DisplayDriver {
 public:
  virtual ~DisplayDriver() = default;
  virtual bool begin() = 0;
  virtual void end() {}
  virtual bool writeData(ISurface<RGB_T>& surface) = 0;
  virtual bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) = 0;

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