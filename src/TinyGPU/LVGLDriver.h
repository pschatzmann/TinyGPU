
#include "TinyGPU.h"
#include "TouchDriver.h"
#include "Vector.h"
#include "lvgl.h"

extern "C" void lvgl_tgpu_output_flush_cb(lv_display_t* disp,
                                          const lv_area_t* area,
                                          uint8_t* px_map);

/**
 * @brief LVGLDriver is a helper class to initialize LVGL v9 with a TinyGPU
 * Output driver. It sets up the display buffer and flush callback for LVGL.
 */
class LVGLDriver {
 public:
  LVGLDriver(DisplayDriverSPI& driver, size_t x, size_t y,
             size_t bufferSize = 0) {
    this->driver = &driver;
    this->disp_x = x;
    this->disp_y = y;
    // Default buffer size in bytes (v9 buffer size is expressed in bytes)
    this->dispBufferSize =
        bufferSize > 0 ? bufferSize : disp_x * sizeof(RGB565);
  }

  // Initializes LVGL and registers the TinyGPU Output driver with LVGL.
  bool begin() {
    if (!driver) {
      return false;
    }

    lv_init();
    lv_tick_set_cb(my_tick);

    driver->begin();

    if (touch_driver) {
      CalibrationData cal;
      cal.screenWidth = disp_x;
      cal.screenHeight = disp_y;
      touch_driver->setCalibration(cal);
      touch_driver->begin();
      setupTouch();
    }

    // Create display object in LVGL v9
    disp = lv_display_create(disp_x, disp_y);
    if (!disp) {
      return false;
    }
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);

    // Allocate draw buffer (byte vector for raw pixel memory)
    buf_1.resize(dispBufferSize);

    // Register display buffer with LVGL v9
    lv_display_set_buffers(disp, buf_1.data(), nullptr, dispBufferSize,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Set user data and flush callback
    lv_display_set_user_data(disp, this);
    lv_display_set_flush_cb(disp, lvgl_tgpu_output_flush_cb);

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
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;
    size_t bytes =
        static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(RGB565);

    LVGLDriver* p_driver =
        static_cast<LVGLDriver*>(lv_display_get_user_data(disp));

    SurfaceWithExternalBuffer<RGB565> surface(static_cast<size_t>(w),
                                              static_cast<size_t>(h));

    // 1. Assign external buffer (pass size in bytes)
    surface.setExternalBuffer(px_map, w * h * sizeof(RGB565));

    // 2. Resize surface dimensions (this updates width_ and height_)
    surface.resizeBuffer(w, h);

    // 3. Draw the surface to the TinyGPU Output
    p_driver->getDriver().writeData(surface, area->x1, area->y1);

    // 4. Notify LVGL
    lv_display_flush_ready(disp);
  }

  void delay(uint32_t ms) {
    lv_tick_inc(5); /* explicitly feed 5ms to LVGL tick */
    delay(5);
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
  DisplayDriverSPI& getDriver() { return *driver; }

 protected:
  Vector<uint8_t> buf_1;
  DisplayDriverSPI* driver = nullptr;
  TouchDriver* touch_driver = nullptr;
  lv_display_t* disp = nullptr;
  size_t disp_x = 0;
  size_t disp_y = 0;
  size_t dispBufferSize = 0;

  // Use Arduino's millis() as tick source
  static uint32_t my_tick(void) { return millis(); }

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

extern "C" void lvgl_tgpu_output_flush_cb(lv_display_t* disp,
                                          const lv_area_t* area,
                                          uint8_t* px_map) {
  LVGLDriver::display(disp, area, px_map);
}