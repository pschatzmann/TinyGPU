#pragma once
#include "TinyGPU/Surface/ISurface.h"
#include "TinyGPU/Surface/SurfaceBase.h"

namespace tinygpu {

/**
 * @brief An ISurface that *reports* a full `width x height` extent but
 * physically redirects every pixel write into a much smaller `backing`
 * surface, offset by `(offsetX, offsetY)`, dropping anything that then
 * falls outside `backing`'s own bounds.
 *
 * Exists so a widget (or a handful of them) can be rendered into one
 * small reused buffer instead of a full-screen framebuffer - see
 * tinymd::Screen::drawDirect(), which uses this two ways:
 *
 *  - Per-widget: `backing` resized to exactly one widget's own bounds,
 *    `offsetX`/`offsetY` at that widget's screen position, `fullWidth`/
 *    `fullHeight` equal to the widget's own w/h - so the widget draws
 *    into what looks to it like a normal, appropriately-sized surface.
 *  - Banded (for a presented modal, whose scrim needs the *full*
 *    viewport's width/height to size correctly - see Dialog.h/Drawer.h -
 *    and which can span most of the screen): `backing` is one short,
 *    fixed-width strip; `offsetY` steps through the viewport a strip at
 *    a time while `fullWidth`/`fullHeight` stay pinned to the *whole*
 *    viewport, so the scrim still fills correctly even though `backing`
 *    only ever holds one strip's worth of pixels.
 *
 * Either way, every higher-level drawing call a widget's draw() makes
 * (fillRect, drawLine, drawCircle, drawText, drawSprite, fillRoundRect,
 * a modal's full-width/height scrim fill, ...) is inherited unchanged
 * from SurfaceBase, which implements all of them purely in terms of
 * setPixel()/getPixel() - the only two primitives overridden here - so
 * nothing being drawn needs to know the difference from a real surface.
 *
 * A widget always draws using *absolute* screen coordinates (its own
 * `this->bounds.x/y`, not 0), so - critically - the `width_`/`height_`
 * SurfaceBase itself uses internally to clip fillRect()/drawSprite()/etc.
 * (inherited, not reimplemented here) must be set to where `backing`'s
 * far edge actually falls *in that same absolute space* -
 * `offsetX + backing.width()`, not just `backing.width()` - or every one
 * of those clips silently rejects nearly everything for any widget not
 * sitting at (0, 0). What width()/height() *report* to callers (a
 * modal's scrim math, chiefly) is a separate, independently overridden
 * pair of accessors below, exactly because the two needs (correct
 * internal clipping vs. "how big does the caller think the canvas is")
 * are different numbers whenever offsetX/offsetY aren't both 0.
 *
 * One instance is (cheaply) constructed per widget/strip; it owns no
 * pixel storage of its own.
 */
template <typename RGB_T = RGB565>
class WindowedSurface : public SurfaceBase<RGB_T> {
 public:
  /// `backing` must outlive this object. `fullWidth`/`fullHeight` are
  /// what width()/height() report (see the class comment for the two
  /// ways callers pick these); `offsetX`/`offsetY` is where `backing`'s
  /// own (0, 0) sits within that full logical space.
  WindowedSurface(ISurface<RGB_T>& backing, int32_t offsetX, int32_t offsetY, size_t fullWidth,
                  size_t fullHeight, IFont<RGB_T>& font)
      : SurfaceBase<RGB_T>(static_cast<size_t>(offsetX) + backing.width(),
                           static_cast<size_t>(offsetY) + backing.height(), font),
        backing_(backing),
        offsetX_(offsetX),
        offsetY_(offsetY),
        fullWidth_(fullWidth),
        fullHeight_(fullHeight) {}

  /// Reports the *logical* size (see class comment) - not the same as
  /// the internal clip bound SurfaceBase's inherited fillRect() etc. use,
  /// when offsetX/offsetY aren't both 0.
  size_t width() const override { return fullWidth_; }
  size_t height() const override { return fullHeight_; }

  void setPixel(size_t x, size_t y, RGB_T color) override {
    const long bx = static_cast<long>(x) - offsetX_;
    const long by = static_cast<long>(y) - offsetY_;
    if (bx < 0 || by < 0) return;
    if (static_cast<size_t>(bx) >= backing_.width() || static_cast<size_t>(by) >= backing_.height()) {
      return;
    }
    backing_.setPixel(static_cast<size_t>(bx), static_cast<size_t>(by), color);
  }

  RGB_T getPixel(size_t x, size_t y) const override {
    const long bx = static_cast<long>(x) - offsetX_;
    const long by = static_cast<long>(y) - offsetY_;
    if (bx < 0 || by < 0) return RGB_T();
    if (static_cast<size_t>(bx) >= backing_.width() || static_cast<size_t>(by) >= backing_.height()) {
      return RGB_T();
    }
    return backing_.getPixel(static_cast<size_t>(bx), static_cast<size_t>(by));
  }

  /// No storage of its own to (re)allocate - the internal clip bound is
  /// fixed at construction (see above). Only exists to satisfy
  /// SurfaceBase's pure-virtual resizeBuffer(); never meaningfully called.
  bool resizeBuffer(size_t, size_t) override { return true; }

  /// Not backed by one contiguous buffer, so there's no single pointer/
  /// size to hand back - nothing in the normal widget draw() path calls
  /// these (pixels go through setPixel()/getPixel() instead), so these
  /// are stubs rather than real implementations.
  const uint8_t* data() const override { return nullptr; }
  size_t size() const override { return 0; }

  /// Absolute-coordinate bounds check, consistent with setPixel()/
  /// getPixel() above (and so with SurfaceBase's inherited fillRect()
  /// etc., which clip against the same underlying width_/height_) -
  /// deliberately *not* fullWidth_/fullHeight_, which is what a caller
  /// (Dialog's/Drawer's own drawArc()-free code never calls contains()
  /// directly, but this keeps the two consistent regardless).
  bool contains(size_t x, size_t y) override {
    return x < SurfaceBase<RGB_T>::width_ && y < SurfaceBase<RGB_T>::height_;
  }

 private:
  ISurface<RGB_T>& backing_;
  int32_t offsetX_;
  int32_t offsetY_;
  size_t fullWidth_;
  size_t fullHeight_;
};

}  // namespace tinygpu
