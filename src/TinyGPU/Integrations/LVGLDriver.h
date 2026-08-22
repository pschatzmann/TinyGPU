
#include <type_traits>

#include "GammaTable.h"
#include "TinyGPU.h"
#include "TouchDriver.h"
#include "Vector.h"
#include "lvgl.h"

/**
 * @brief LVGLDriver is a helper class to initialize LVGL v9 with a TinyGPU
 * Output driver. It sets up the display buffer and flush callback for LVGL.
 *
 * @tparam RGB_T The pixel format LVGL's rendered output is converted to
 * before transmission. Defaults to RGB565, matching LVGL's own internal
 * RGB565 color format (convertBuffer() below still has to undo RGB565's
 * own byte-swapped storage - see RGB565.h - even in that default case).
 */
template <typename RGB_T = RGB565>
class LVGLDriver {
  static_assert(sizeof(RGB_T) == 2,
               "LVGLDriver currently hardcodes LV_COLOR_FORMAT_RGB565 (a "
               "16-bit format) for LVGL's own rendering, so RGB_T must also "
               "be a 16-bit pixel type.");

 public:
  LVGLDriver(DisplayDriverSPI<RGB_T>& driver, size_t x, size_t y,
             size_t bufferSize = 0) {
    this->driver = &driver;
    this->disp_x = x;
    this->disp_y = y;
    // Default buffer size in bytes (v9 buffer size is expressed in bytes)
    this->dispBufferSize =
        bufferSize > 0 ? bufferSize : disp_x * sizeof(RGB_T);
  }

  // Initializes LVGL and registers the TinyGPU Output driver with LVGL.
  bool begin() {
    if (!driver) {
      return false;
    }

    lv_init();
    lv_tick_set_cb(my_tick);

    driver->begin();

    // Create display object in LVGL v9
    disp = lv_display_create(disp_x, disp_y);
    if (!disp) {
      return false;
    }
    // This is LVGL's own internal rendering format, independent of RGB_T -
    // see the note on display() below.
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    // Allocate draw buffer (byte vector for raw pixel memory)
    buf_1.resize(dispBufferSize);

    // Register display buffer with LVGL v9
    lv_display_set_buffers(disp, buf_1.data(), nullptr, dispBufferSize,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Set user data and flush callback. LVGLDriver<RGB_T>::display is a
    // static, non-capturing member function, so its address is a plain
    // function pointer LVGL's C API can call directly - no free-standing
    // forwarding function needed.
    lv_display_set_user_data(disp, this);
    lv_display_set_flush_cb(disp, &LVGLDriver::display);

    // setupTouch() registers an LVGL input device against `disp`, so it
    // must run after disp exists - it silently does nothing otherwise.
    if (touch_driver) {
      // Calibration is only meaningful for controllers that report raw,
      // uncalibrated ADC counts (e.g. resistive XPT2046 panels). Capacitive
      // controllers (CST816S, FT6236, ...) already report coordinates in
      // native screen-pixel space, so forcing a canned calibration profile
      // here would corrupt their output. If your touch driver needs
      // calibration, call touchDriver.setCalibration(...) yourself before
      // passing it to setTouchDriver().
      if (!touch_driver->hasCalibration()) {
        Serial.println(
            "LVGLDriver: touch driver has no calibration set. If it reports "
            "raw ADC counts (e.g. a resistive XPT2046 panel) rather than "
            "screen-pixel coordinates, touch input will be wrong until you "
            "call touchDriver.setCalibration(...) before setTouchDriver().");
      }
      touch_driver->begin();
      setupTouch();
    }

    return true;
  }

  void end() {
    if (disp) {
      lv_display_delete(disp);
      disp = nullptr;
    }
    lv_tick_set_cb(nullptr);
    lv_deinit();
    buf_1.clear();
  }

  // Displays the given area of the screen with the provided color data.
  static void display(lv_display_t* disp, const lv_area_t* area,
                      uint8_t* px_map) {
    assert(disp != nullptr);
    const size_t w = static_cast<size_t>(area->x2 - area->x1 + 1);
    const size_t h = static_cast<size_t>(area->y2 - area->y1 + 1);

    LVGLDriver* p_driver =
        static_cast<LVGLDriver*>(lv_display_get_user_data(disp));

    // px_map is packed by LVGL itself according to LV_COLOR_FORMAT_RGB565,
    // which is LVGL's own fixed (native-order) bit layout - unrelated to
    // RGB_T's own storage convention. Re-pack each pixel into RGB_T's
    // layout (and apply gamma correction, if set) before handing the
    // buffer to the driver.
    convertBuffer(px_map, w * h);

    // SurfaceWithExternalBuffer's default font argument is hardcoded to
    // BitmapFont<RGB565>, which doesn't match IFont<RGB_T>& for any other
    // RGB_T, so an explicit RGB_T-typed font has to be passed here.
    static BitmapFont<RGB_T> font;
    SurfaceWithExternalBuffer<RGB_T> surface(w, h, font);
    surface.setExternalBuffer(px_map, w * h * sizeof(RGB_T));
    surface.resizeBuffer(w, h);

    p_driver->getDriver().writeData(surface, area->x1, area->y1);

    lv_display_flush_ready(disp);
  }

  void delay(uint32_t ms) {
    lv_tick_inc(ms);
    // Unqualified delay(ms) here would resolve to this very member
    // function (C++ name lookup stops at the first enclosing scope with a
    // matching name, regardless of parameter types), recursing forever
    // instead of calling Arduino's delay(). :: forces the global one.
    ::delay(ms);
  }

  /// Defines the touch driver to be used with LVGL. This is optional and can be
  /// set if a touch driver is available.
  void setTouchDriver(TouchDriver& touch) { touch_driver = &touch; }

  /// Checks if a touch driver has been set for this LVGLDriver.
  bool hasTouchDriver() const { return touch_driver != nullptr; }

  /// Provides a pointer to the touch driver, if one has been set. Returns
  /// nullptr if no touch driver is available.
  TouchDriver& touchDriver() { return *touch_driver; }

  /// Provides a reference to the underlying DisplayDriverSPI instance used by
  /// this LVGLDriver.
  DisplayDriverSPI<RGB_T>& getDriver() { return *driver; }

  /// Applies independent per-channel gamma correction to every color LVGL
  /// renders, before RGB_T's own field layout/compensation is applied.
  /// gammaR=gammaG=gammaB=1.0 (the default) applies no correction.
  ///
  /// This is deliberately per-channel rather than one shared curve: some
  /// panels show a visible color tint in near-black/grey tones even with
  /// correct field compensation, because the panel's red/green/blue
  /// subpixels have different non-linear responses at low brightness. A
  /// single gamma applied equally to R, G and B can only ever change
  /// overall brightness - if R=G=B going in, the same curve on all three
  /// still gives R=G=B going out, so it mathematically cannot fix a hue
  /// tint in grey tones. Independent per-channel curves can, by
  /// deliberately un-balancing a grey input to counteract the panel's own
  /// imbalance. There's no single correct set of values for an unknown/
  /// undocumented panel - try values on both sides of 1.0 per channel and
  /// compare on real hardware. See GammaTable.h.
  static void setGamma(float gammaR, float gammaG, float gammaB) {
    gammaR_().setGamma(gammaR);
    gammaG_().setGamma(gammaG);
    gammaB_().setGamma(gammaB);
  }

 protected:
  Vector<uint8_t> buf_1;
  DisplayDriverSPI<RGB_T>* driver = nullptr;
  TouchDriver* touch_driver = nullptr;
  lv_display_t* disp = nullptr;
  size_t disp_x = 0;
  size_t disp_y = 0;
  size_t dispBufferSize = 0;

  // Use Arduino's millis() as tick source
  static uint32_t my_tick(void) { return millis(); }

  static GammaTable& gammaR_() {
    static GammaTable table;
    return table;
  }
  static GammaTable& gammaG_() {
    static GammaTable table;
    return table;
  }
  static GammaTable& gammaB_() {
    static GammaTable table;
    return table;
  }

  /// Converts a buffer LVGL packed as (native-order) RGB565 into RGB_T's
  /// own byte layout, applying per-channel gamma correction (see
  /// setGamma()), in place. Each pixel is decoded with RGB565's semantics
  /// (what LVGL actually wrote), gamma corrected, and re-encoded via
  /// RGB_T::fromRGB(), so RGB_T's own compensation (if any) is genuinely
  /// applied regardless of how RGB_T's constructor happens to order its
  /// arguments.
  static void convertBuffer(uint8_t* px_map, size_t pixelCount) {
    const bool sameFormat = std::is_same<RGB_T, RGB565>::value;
    const bool needsGamma = (gammaR_().gamma() != 1.0f) ||
                            (gammaG_().gamma() != 1.0f) ||
                            (gammaB_().gamma() != 1.0f);
    uint16_t* pixels = reinterpret_cast<uint16_t*>(px_map);
    if (sameFormat && !needsGamma) {
      // No channel repacking or gamma to apply, but RGB565 stores bytes
      // swapped from LVGL's native order (see RGB565.h) - a raw swap is
      // still needed.
      for (size_t i = 0; i < pixelCount; ++i) {
        pixels[i] = RGB565::swapBytes(pixels[i]);
      }
      return;
    }

    const GammaTable& gr = gammaR_();
    const GammaTable& gg = gammaG_();
    const GammaTable& gb = gammaB_();
    for (size_t i = 0; i < pixelCount; ++i) {
      // pixels[i] is LVGL's native-order pixel; byte-swap it into
      // RGB565's stored (wire) order before decoding.
      const RGB565 src(RGB565::swapBytes(pixels[i]));
      uint8_t r = src.getRed();
      uint8_t g = src.getGreen();
      uint8_t b = src.getBlue();
      if (needsGamma) {
        r = gr.apply(r);
        g = gg.apply(g);
        b = gb.apply(b);
      }
      const RGB_T dst = RGB_T::fromRGB(r, g, b);
      pixels[i] = dst.getValue();
    }
  }

  /**
   * @brief Registers a touch driver with this LVGL display instance.
   */
  lv_indev_t* setupTouch() {
    if (!disp) return nullptr;

    lv_indev_t* indev = lv_indev_create();
    if (!indev) return nullptr;

    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, disp);  // Binds touch to this specific display
    lv_indev_set_user_data(indev, &touchDriver());

    // Set static touch read callback
    lv_indev_set_read_cb(indev, [](lv_indev_t* indev, lv_indev_data_t* data) {
      auto* touch =
          static_cast<TouchDriver*>(lv_indev_get_user_data(indev));
      Point p;
      if (touch && touch->getPoint(p)) {
        data->point.x = p.x;
        data->point.y = p.y;
        data->state = LV_INDEV_STATE_PRESSED;
      } else {
        data->state = LV_INDEV_STATE_RELEASED;
      }
    });

    return indev;
  }

  void dumpBuffer(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      if ((i % 16) == 0) {
        Serial.printf("\n%06u: ", (unsigned)i);
      }

      Serial.printf("%02X ", data[i]);
    }

    Serial.println();
  }
};
