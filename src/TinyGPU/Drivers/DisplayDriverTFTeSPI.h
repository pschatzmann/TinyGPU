#pragma once
#include <TFT_eSPI.h>  // https://github.com/Bodmer/TFT_eSPI

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Color/RGB565.h"

namespace tinygpu {

/**
 * @brief Display driver that renders a TinyGPU surface through Bodmer's
 * TFT_eSPI (https://github.com/Bodmer/TFT_eSPI) instead of one of this
 * library's own SPI panel drivers (DisplayDriverSPI/QSPI/DSI) - useful if
 * you already have a working TFT_eSPI `User_Setup.h` for your panel and
 * would rather reuse it than duplicate the same init sequence/pin wiring
 * here.
 *
 * TFT_eSPI is a separate, optional Arduino library - install it alongside
 * TinyGPU and `#include <TinyGPU/Drivers/DisplayDriverTFTeSPI.h>`
 * explicitly (this header is not pulled in by TinyGPU/DisplayDriver.h or
 * TinyGPU.h) to opt in. All of the panel-specific configuration (which
 * controller, which pins, SPI frequency, ...) is TFT_eSPI's own
 * `User_Setup.h`/`User_Setup_Select.h` job, same as any other TFT_eSPI
 * sketch - this driver only bridges TinyGPU's ISurface/writeData() calls
 * onto TFT_eSPI's own API.
 *
 * Usage:
 *   TFT_eSPI tft;
 *   DisplayDriverTFTeSPI<RGB565> display(tft);
 *   display.begin();  // calls tft.init() and tft.setRotation()
 *   display.writeData(surface);
 *
 * Only a 16bpp RGB_T (RGB565) is supported, since that's the only pixel
 * format TFT_eSPI's pushImage() accepts.
 */
template <typename RGB_T = RGB565>
class DisplayDriverTFTeSPI : public DisplayDriver<RGB_T> {
  static_assert(sizeof(RGB_T) == 2,
               "DisplayDriverTFTeSPI assumes a 16bpp RGB_T (RGB565) - "
               "TFT_eSPI::pushImage() only accepts that format.");

 public:
  /// Wraps an existing TFT_eSPI instance (ownership not taken - construct
  /// and keep it alive for as long as this driver is used, the same way
  /// DisplayDriverSPI takes its SPIClass& rather than owning one).
  explicit DisplayDriverTFTeSPI(TFT_eSPI& tft,
                                DisplayRotation rotation = DisplayRotation::kNone)
      : tft_(tft), rotation_(rotation) {}

  /// Initializes TFT_eSPI (tft.init()) and applies the rotation passed to
  /// the constructor, if any.
  bool begin() override {
    tft_.init();
    if (rotation_ != DisplayRotation::kNone) {
      setRotation(rotation_);
    }
    return true;
  }

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    // RGB_T (RGB565) values are stored in the buffer already byte-swapped
    // to the panel's wire order (see RGB565.h) - the same big-endian
    // order TFT_eSPI::pushImage() expects by default (setSwapBytes(true)
    // is only needed for sources in the *native*/little-endian 16-bit
    // layout, e.g. raw LVGL buffers - not this one), so the buffer's raw
    // memory can be handed to pushImage() as-is, with no per-pixel swap.
    tft_.pushImage(static_cast<int32_t>(x), static_cast<int32_t>(y),
                   static_cast<int32_t>(surface.width()),
                   static_cast<int32_t>(surface.height()),
                   reinterpret_cast<const uint16_t*>(surface.data()));
    return true;
  }

  /// Changes the panel's rotation - forwards directly to
  /// TFT_eSPI::setRotation(), whose 0-3 values already match
  /// DisplayRotation's own kPortrait/kLandscape/kPortraitFlipped/
  /// kLandscapeFlipped numbering. kNone is a no-op (nothing to apply).
  void setRotation(DisplayRotation rotation) override {
    if (rotation == DisplayRotation::kNone) return;
    rotation_ = rotation;
    tft_.setRotation(static_cast<uint8_t>(rotation));
  }

  size_t width() const override { return static_cast<size_t>(tft_.width()); }
  size_t height() const override {
    return static_cast<size_t>(tft_.height());
  }

 protected:
  TFT_eSPI& tft_;
  DisplayRotation rotation_;

  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    tft_.setAddrWindow(static_cast<int32_t>(x), static_cast<int32_t>(y),
                       static_cast<int32_t>(w), static_cast<int32_t>(h));
    return true;
  }
};

}  // namespace tinygpu
