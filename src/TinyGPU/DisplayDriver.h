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

 protected:
  virtual bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) = 0;
};

}  // namespace tinygpu