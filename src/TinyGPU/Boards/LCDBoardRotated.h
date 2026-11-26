// TinyGPU - 90 degree rotation of any LCDBoard.
//
// A board with a landscape panel can show the UI in portrait (or the other way
// round). LCDBoardRotated wraps a board and reports the rotated size. The image
// is rotated in hardware if the display driver supports setRotation() (its
// width() and height() change); otherwise it is rotated in software, one row at a
// time, so no frame buffer is needed. Touch points are mapped to the same
// rotated coordinates.
//
//   LCDBoardDesktopSDL board(320, 240);     // landscape panel
//   LCDBoardRotated rotated(board);         // 240 x 320 portrait UI
//   tinymd::App<RGB565> app(rotated, theme);
//
// Rotations (the UI is portrait):
//   kPortrait          - clockwise: the top left of the UI is at the top right of the panel
//   kPortraitFlipped   - counter-clockwise: the top left of the UI is at the bottom left
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "TinyGPU/Boards/LCDBoardsCommon.h"
#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Input/TouchDriverCommon.h"
#include "TinyGPU/Surface/Surface.h"

namespace tinygpu {

namespace detail {

// Maps a portrait coordinate to the landscape panel (rotation: kPortrait or kPortraitFlipped).
// portraitW/H are the rotated (UI) size; the panel is portraitH x portraitW.
inline void rotatePoint(DisplayRotation rotation, size_t portraitW, size_t portraitH, size_t px, size_t py,
                        size_t& lx, size_t& ly) {
  if (rotation == DisplayRotation::kPortraitFlipped) {
    lx = py;
    ly = portraitW - 1 - px;
  } else {
    lx = portraitH - 1 - py;
    ly = px;
  }
}

// The inverse of rotatePoint(): panel coordinates to the UI.
inline void unrotatePoint(DisplayRotation rotation, size_t portraitW, size_t portraitH, size_t lx, size_t ly,
                          size_t& px, size_t& py) {
  if (rotation == DisplayRotation::kPortraitFlipped) {
    px = portraitW - 1 - ly;
    py = lx;
  } else {
    px = ly;
    py = portraitH - 1 - lx;
  }
}

}  // namespace detail

/// A display driver that shows its UI rotated by 90 degrees. Hardware rotation is
/// used when the driver supports it (see LCDBoardRotated); otherwise the rows are
/// rotated in software when they are written.
class RotatedDisplay : public DisplayDriver<RGB565> {
 public:
  RotatedDisplay(DisplayDriver<RGB565>& panel, size_t portraitW, size_t portraitH, DisplayRotation rotation)
      : panel_(panel), portraitW_(portraitW), portraitH_(portraitH), rotation_(rotation) {}

  /// The board begins the panel; this only exists for the App, which calls it too.
  bool begin() override { return true; }

  bool writeData(ISurface<RGB565>& surface) override { return writeData(surface, 0, 0); }

  /// Writes the UI region at (x, y) (UI coordinates).
  bool writeData(ISurface<RGB565>& surface, size_t x, size_t y) override {
    if (hardware_) return panel_.writeData(surface, x, y);
    const size_t bw = surface.width(), bh = surface.height();
    if (bw == 0 || bh == 0) return true;
    // a row of the panel is one column of the UI region
    Surface<RGB565> row(bh, 1, defaultFont);
    if (!row.begin()) return false;
    if (rotation_ == DisplayRotation::kPortraitFlipped) {
      // panel column y+k is the UI row k of the region, read from the right
      const size_t ly0 = portraitW_ - (x + bw), lx0 = y;
      for (size_t r = 0; r < bw; ++r) {
        const size_t sx = bw - 1 - r;
        for (size_t k = 0; k < bh; ++k) row.setPixel(k, 0, surface.getPixel(sx, k));
        if (!panel_.writeData(row, lx0, ly0 + r)) return false;
      }
    } else {
      // the UI column x+r runs down the panel row x+r, from the bottom up
      const size_t ly0 = x, lx0 = portraitH_ - (y + bh);
      for (size_t r = 0; r < bw; ++r) {
        for (size_t k = 0; k < bh; ++k) row.setPixel(k, 0, surface.getPixel(r, bh - 1 - k));
        if (!panel_.writeData(row, lx0, ly0 + r)) return false;
      }
    }
    return true;
  }

  /// The UI size (the rotated size).
  size_t width() const override { return portraitW_; }
  size_t height() const override { return portraitH_; }

  /// True if the panel rotates the image itself.
  bool hardwareRotation() const { return hardware_; }
  void setHardwareRotation(bool hardware) { hardware_ = hardware; }
  DisplayRotation rotation() const { return rotation_; }

 protected:
  bool setAddressWindow(size_t, size_t, size_t, size_t) override { return true; }

 private:
  DisplayDriver<RGB565>& panel_;
  size_t portraitW_;
  size_t portraitH_;
  DisplayRotation rotation_;
  bool hardware_ = false;
};

/// A touch controller whose points are in the rotated (UI) coordinates.
class RotatedTouch : public TouchDriver {
 public:
  RotatedTouch(TouchDriver& touch, size_t portraitW, size_t portraitH, DisplayRotation rotation)
      : touch_(touch), portraitW_(portraitW), portraitH_(portraitH), rotation_(rotation) {}

  bool begin() override { return true; }
  bool isTouched() override { return touch_.isTouched(); }

  bool getPoint(Point& outPoint) override {
    Point p;
    if (!touch_.getPoint(p)) return false;
    size_t px = 0, py = 0;
    detail::unrotatePoint(rotation_, portraitW_, portraitH_, static_cast<size_t>(p.x), static_cast<size_t>(p.y), px,
                          py);
    outPoint = p;
    outPoint.x = static_cast<int16_t>(px);
    outPoint.y = static_cast<int16_t>(py);
    return true;
  }

 private:
  TouchDriver& touch_;
  size_t portraitW_;
  size_t portraitH_;
  DisplayRotation rotation_;
};

/// Wraps a board: its width() and height() are the rotated size when the panel is
/// landscape. The rotation is applied in hardware when the display driver supports
/// it, otherwise in software.
class LCDBoardRotated : public LCDBoard {
 public:
  /// Wraps `board`, which must outlive this object. A panel that is already portrait
  /// is not rotated.
  explicit LCDBoardRotated(LCDBoard& board, DisplayRotation rotation = DisplayRotation::kPortrait)
      : board_(board),
        rotated_(board.width() > board.height()),
        display_(board.display(), rotated_ ? board.height() : board.width(), rotated_ ? board.width() : board.height(),
                 rotation),
        touch_(board.touch() != nullptr
                   ? new RotatedTouch(*board.touch(), display_.width(), display_.height(), rotation)
                   : nullptr) {}

  ~LCDBoardRotated() override { delete touch_; }

  /// Begins the board, then asks the panel to rotate itself. If the panel's size does
  /// not change, the rotation is done in software.
  bool begin() override {
    if (!board_.begin()) return false;
    if (rotated_) {
      DisplayDriver<RGB565>& panel = board_.display();
      panel.setRotation(display_.rotation());
      display_.setHardwareRotation(panel.width() == display_.width() && panel.height() == display_.height());
    }
    return true;
  }

  size_t width() const override { return rotated_ ? display_.width() : board_.width(); }
  size_t height() const override { return rotated_ ? display_.height() : board_.height(); }

  /// The rotated display, or the board's own display if the panel is portrait.
  DisplayDriver<RGB565>& display() override { return rotated_ ? static_cast<DisplayDriver<RGB565>&>(display_) : board_.display(); }
  TouchDriver* touch() override { return rotated_ && touch_ != nullptr ? touch_ : board_.touch(); }
  const I2SPins& i2s() const override { return board_.i2s(); }
  const LEDPins& led() const override { return board_.led(); }
  int8_t backlightPin() const override { return board_.backlightPin(); }

  /// True if the board was rotated (the panel is landscape).
  bool rotated() const { return rotated_; }
  /// True if the panel rotates the image itself (valid after begin()).
  bool hardwareRotation() const { return display_.hardwareRotation(); }

 private:
  LCDBoard& board_;
  bool rotated_;
  RotatedDisplay display_;
  RotatedTouch* touch_;
};

}  // namespace tinygpu
