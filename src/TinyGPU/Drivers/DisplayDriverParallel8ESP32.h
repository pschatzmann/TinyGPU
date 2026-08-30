#pragma once
#include <initializer_list>
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Emulation.h"

#if defined(ESP32)
#include "esp_lcd_panel_io.h"
#include "soc/soc_caps.h"
// Both headers are safe to include unconditionally - the declarations
// inside each are themselves guarded by the matching SOC_..._SUPPORTED
// macro, so only the one the target chip actually has compiles to
// anything.
#include "esp_lcd_io_i80.h"
#include "esp_lcd_io_parl.h"
#define TINYGPU_HAS_ESP_LCD_PARALLEL8 1
#else
#error \
    "DisplayDriverParallel8ESP32.h needs the ESP-IDF esp_lcd component (LCD_CAM/I80 or PARLIO peripheral) - ESP32-only. For a portable, any-Arduino-core bit-banged 8-bit parallel driver see DisplayDriverParallel8.h instead."
#endif

namespace tinygpu {

#ifdef TINYGPU_HAS_ESP_LCD_PARALLEL8

#if SOC_LCD_I80_SUPPORTED
#define TINYGPU_PARALLEL8_BACKEND_I80 1
#elif SOC_PARLIO_SUPPORTED
#define TINYGPU_PARALLEL8_BACKEND_PARLIO 1
#else
#error \
    "This ESP32 target has neither the LCD_CAM/I80 peripheral (SOC_LCD_I80_SUPPORTED) nor the PARLIO peripheral (SOC_PARLIO_SUPPORTED) - hardware-accelerated 8-bit parallel output isn't available on it. Use the portable bit-banged DisplayDriverParallel8.h instead."
#endif

/**
 * @brief Hardware-accelerated 8-bit parallel ("Intel 8080"-style) TFT
 * display driver for ESP32-family targets, built on ESP-IDF's esp_lcd
 * panel-IO layer - the same layer DisplayDriverQSPI.h uses for QSPI, just
 * pointed at a different underlying peripheral. Where DisplayDriverSPI.h's
 * hardware-parallel counterpart (DisplayDriverParallel8.h) bit-bangs D0-D7
 * one digitalWrite() per bit, this class drives the bus via DMA through
 * whichever peripheral the target actually has:
 *
 *   - SOC_LCD_I80_SUPPORTED targets (ESP32, ESP32-S2, ESP32-S3): the
 *     LCD_CAM peripheral's I80 mode, via esp_lcd_new_i80_bus() +
 *     esp_lcd_new_panel_io_i80().
 *   - SOC_PARLIO_SUPPORTED targets (ESP32-C6, ESP32-H2, ESP32-P4, ...,
 *     which have no LCD_CAM): the PARLIO peripheral's 8-bit parallel LCD
 *     mode, via esp_lcd_new_panel_io_parl().
 *
 * Which backend compiles is decided automatically from the target's
 * soc_caps.h (TINYGPU_PARALLEL8_BACKEND_I80 / _PARLIO, set above) - the
 * public API (constructor, writeCommand()/writeData()/setAddressWindow())
 * is identical either way, so concrete panel drivers below don't need to
 * know or care which peripheral is underneath.
 *
 * Build-verified (compiles and links) for the I80 backend via arduino-cli
 * against esp32:esp32 3.3.10 on an ESP32-S3 target
 * (esp32:esp32:adafruit_metro_esp32s3). The PARLIO backend's source
 * compiles cleanly against the same core/version on an ESP32-C6 target
 * (esp32:esp32:esp32c6), but fails to *link* there:
 * esp_lcd_new_panel_io_parl is declared in that core's esp_lcd headers but
 * missing from its prebuilt esp32-arduino-libs static libraries for C6 -
 * an SDK-packaging gap in that specific core version, not a bug in this
 * file. Verify linking against whatever core version you actually build
 * with before relying on the PARLIO path; a plain ESP-IDF build (rather
 * than arduino-esp32) may not have this gap.
 *
 * Subclasses call the inherited beginBus() first thing in their begin()
 * (setting up the bus/panel-IO), then send their chip's own init register
 * sequence via writeCommand() - the same division of labor
 * DisplayDriverQSPI.h's beginBus()/writeCommand() split uses.
 */
template <typename RGB_T = RGB565>
class DisplayDriverParallel8ESP32 : public DisplayDriver<RGB_T> {
 public:
  /// @param d0..d7 the 8 data lines, D0 = least significant bit.
  /// @param wr write-strobe pin (WR on I80, wired to PARLIO's clk_gpio_num
  /// on PARLIO targets - PARLIO calls the same physical strobe line "clk").
  /// @param dc data/command ("RS") select pin.
  /// @param cs chip-select pin, or -1 to declare exclusive bus ownership
  /// (I80 backend only - the PARLIO backend always wants a real CS pin).
  /// @param width/height the panel's addressable resolution - subclasses
  /// with a rotation concept (e.g. ILI9341Driver8080ESP32) override
  /// width()/height() themselves instead and can ignore these.
  DisplayDriverParallel8ESP32(int8_t d0, int8_t d1, int8_t d2, int8_t d3,
                              int8_t d4, int8_t d5, int8_t d6, int8_t d7,
                              int8_t wr, int8_t dc, int8_t cs,
                              size_t width = 240, size_t height = 320,
                              uint32_t pclkHz = 20000000)
      : d_{d0, d1, d2, d3, d4, d5, d6, d7},
        wr_(wr),
        dc_(dc),
        cs_(cs),
        width_(width),
        height_(height),
        pclkHz_(pclkHz) {}

  void end() override {
    if (io_ != nullptr) {
      esp_lcd_panel_io_del(io_);
      io_ = nullptr;
    }
#ifdef TINYGPU_PARALLEL8_BACKEND_I80
    if (bus_ != nullptr) {
      esp_lcd_del_i80_bus(bus_);
      bus_ = nullptr;
    }
#endif
  }

  ~DisplayDriverParallel8ESP32() override {
    end();
    delete[] scratch_;
  }

  size_t width() const override { return width_; }
  size_t height() const override { return height_; }

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    static_assert(sizeof(RGB_T) == 2,
                  "writeData assumes a 16bpp RGB_T (RGB565) stored in "
                  "wire byte order");
    if (io_ == nullptr) return false;
    if (!setAddressWindow(x, y, surface.width(), surface.height())) {
      return false;
    }

    // Same reasoning as DisplayDriverQSPI::writeData(): tx_color() is
    // asynchronous, so this needs a persistent scratch buffer (not one
    // freed/reused based on when a previous transfer happens to finish)
    // and blocks on the completion callback rather than chunking the
    // transfer at this level - chunking would re-send the 0x2C RAM-write
    // command per chunk, resetting the panel's write cursor each time.
    const size_t n = surface.size();
    if (scratchCapacity_ < n) {
      delete[] scratch_;
      scratch_ = new uint8_t[n];
      scratchCapacity_ = n;
    }
    memcpy(scratch_, surface.data(), n);

    colorTransPending_ = true;
    if (esp_lcd_panel_io_tx_color(io_, kCmdRamWrite, scratch_, n) !=
        ESP_OK) {
      colorTransPending_ = false;
      return false;
    }
    while (colorTransPending_) {
      // busy-wait for the DMA transfer to finish
    }
    return true;
  }

 protected:
  static constexpr uint8_t kCmdRamWrite = 0x2C;

  int8_t d_[8];
  int8_t wr_, dc_, cs_;
  size_t width_, height_;
  uint32_t pclkHz_;
  esp_lcd_panel_io_handle_t io_ = nullptr;
#ifdef TINYGPU_PARALLEL8_BACKEND_I80
  esp_lcd_i80_bus_handle_t bus_ = nullptr;
#endif
  uint8_t* scratch_ = nullptr;
  size_t scratchCapacity_ = 0;
  volatile bool colorTransPending_ = false;

  /// Sets up the bus (I80 or PARLIO, picked at compile time above) and the
  /// panel-IO layer. Subclasses call this first thing in their begin(),
  /// then send their chip's own init register sequence via writeCommand()
  /// before returning.
  bool beginBus() {
#if defined(TINYGPU_PARALLEL8_BACKEND_I80)
    esp_lcd_i80_bus_config_t busCfg = {};
    busCfg.dc_gpio_num = static_cast<gpio_num_t>(dc_);
    busCfg.wr_gpio_num = static_cast<gpio_num_t>(wr_);
    busCfg.clk_src = LCD_CLK_SRC_DEFAULT;
    for (int i = 0; i < 8; ++i) {
      busCfg.data_gpio_nums[i] = static_cast<gpio_num_t>(d_[i]);
    }
    busCfg.bus_width = 8;
    busCfg.max_transfer_bytes =
        static_cast<size_t>(width_ * height_ * sizeof(RGB_T));
    busCfg.dma_burst_size = 64;

    if (esp_lcd_new_i80_bus(&busCfg, &bus_) != ESP_OK) {
      return false;
    }

    esp_lcd_panel_io_i80_config_t ioCfg = {};
    ioCfg.cs_gpio_num = static_cast<gpio_num_t>(cs_);
    ioCfg.pclk_hz = pclkHz_;
    ioCfg.trans_queue_depth = 10;
    ioCfg.on_color_trans_done =
        &DisplayDriverParallel8ESP32<RGB_T>::onColorTransDone;
    ioCfg.user_ctx = this;
    ioCfg.lcd_cmd_bits = 8;
    ioCfg.lcd_param_bits = 8;
    ioCfg.dc_levels.dc_idle_level = 0;
    ioCfg.dc_levels.dc_cmd_level = 0;
    ioCfg.dc_levels.dc_dummy_level = 0;
    ioCfg.dc_levels.dc_data_level = 1;

    return esp_lcd_new_panel_io_i80(bus_, &ioCfg, &io_) == ESP_OK;
#elif defined(TINYGPU_PARALLEL8_BACKEND_PARLIO)
    esp_lcd_panel_io_parl_config_t ioCfg = {};
    ioCfg.dc_gpio_num = static_cast<gpio_num_t>(dc_);
    ioCfg.clk_gpio_num = static_cast<gpio_num_t>(wr_);
    ioCfg.cs_gpio_num = static_cast<gpio_num_t>(cs_);
    for (int i = 0; i < 8; ++i) {
      ioCfg.data_gpio_nums[i] = static_cast<gpio_num_t>(d_[i]);
    }
    ioCfg.data_width = 8;
    ioCfg.pclk_hz = pclkHz_;
    ioCfg.clk_src = PARLIO_CLK_SRC_DEFAULT;
    ioCfg.max_transfer_bytes =
        static_cast<size_t>(width_ * height_ * sizeof(RGB_T));
    ioCfg.dma_burst_size = 64;
    ioCfg.trans_queue_depth = 10;
    ioCfg.lcd_cmd_bits = 8;
    ioCfg.lcd_param_bits = 8;
    ioCfg.dc_levels.dc_cmd_level = 0;
    ioCfg.dc_levels.dc_data_level = 1;

    return esp_lcd_new_panel_io_parl(&ioCfg, &io_) == ESP_OK;
#endif
  }

  bool writeCommand(uint8_t cmd, const uint8_t* param = nullptr,
                    size_t len = 0) {
    return esp_lcd_panel_io_tx_param(io_, cmd, param, len) == ESP_OK;
  }

  /// Convenience for an init sequence's fixed, short parameter lists -
  /// mirrors DisplayDriverSPI::writeDataN()/DisplayDriverQSPI's inline
  /// {data} calls, just spelled as one call instead of N.
  bool writeCommand(uint8_t cmd, std::initializer_list<uint8_t> data) {
    uint8_t buf[16];
    size_t i = 0;
    for (uint8_t b : data) {
      if (i >= sizeof(buf)) break;  // no init sequence below needs more
      buf[i++] = b;
    }
    return writeCommand(cmd, buf, i);
  }

  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    const uint8_t caset[4] = {
        static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x & 0xFF),
        static_cast<uint8_t>((x + w - 1) >> 8),
        static_cast<uint8_t>((x + w - 1) & 0xFF)};
    const uint8_t raset[4] = {
        static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y & 0xFF),
        static_cast<uint8_t>((y + h - 1) >> 8),
        static_cast<uint8_t>((y + h - 1) & 0xFF)};
    return writeCommand(0x2A, caset, 4) && writeCommand(0x2B, raset, 4);
  }

 private:
  // Runs in ISR context (per esp_lcd_panel_io_color_trans_done_cb_t's
  // contract) - keep this fast. See writeData() for why this exists.
  static bool onColorTransDone(esp_lcd_panel_io_handle_t /*panel_io*/,
                               esp_lcd_panel_io_event_data_t* /*edata*/,
                               void* userCtx) {
    static_cast<DisplayDriverParallel8ESP32<RGB_T>*>(userCtx)
        ->colorTransPending_ = false;
    return false;  // no higher-priority task woken
  }
};

/**
 * @brief Driver for ILI9341 8-bit parallel display controller, driven via
 * ESP32's LCD_CAM/I80 or PARLIO peripheral (see
 * DisplayDriverParallel8ESP32).
 *
 * Same power/gamma/MADCTL init sequence as DisplayDriverSPI.h's
 * ILI9341Driver (see that class's doc comment for provenance) - only the
 * bus underneath differs.
 */
template <typename RGB_T = RGB565>
class ILI9341Driver8080ESP32 : public DisplayDriverParallel8ESP32<RGB_T> {
 public:
  using DisplayDriverParallel8ESP32<RGB_T>::beginBus;
  using DisplayDriverParallel8ESP32<RGB_T>::writeCommand;

  using Rotation = tinygpu::DisplayRotation;

  /// @param nativeWidth/nativeHeight the panel's physical resolution in
  /// its native (portrait, MV bit clear) orientation - 240x320 is the
  /// common ILI9341 module size and the default; pass your panel's real
  /// values if it differs.
  ILI9341Driver8080ESP32(int8_t d0, int8_t d1, int8_t d2, int8_t d3,
                         int8_t d4, int8_t d5, int8_t d6, int8_t d7,
                         int8_t wr, int8_t dc, int8_t cs,
                         Rotation rotation = Rotation::kNone,
                         uint32_t pclkHz = 20000000, size_t nativeWidth = 240,
                         size_t nativeHeight = 320)
      : DisplayDriverParallel8ESP32<RGB_T>(d0, d1, d2, d3, d4, d5, d6, d7, wr,
                                           dc, cs, nativeWidth, nativeHeight,
                                           pclkHz),
        rotation_(rotation),
        nativeWidth_(nativeWidth),
        nativeHeight_(nativeHeight) {}

  size_t width() const override {
    return isLandscapeFamily(rotation_) ? nativeHeight_ : nativeWidth_;
  }
  size_t height() const override {
    return isLandscapeFamily(rotation_) ? nativeWidth_ : nativeHeight_;
  }

  bool begin() override {
    if (!beginBus()) return false;

    writeCommand(0x01);  // SWRESET
    delay(150);

    writeCommand(0xEF, {0x03, 0x80, 0x02});
    writeCommand(0xCF, {0x00, 0xC1, 0x30});
    writeCommand(0xED, {0x64, 0x03, 0x12, 0x81});
    writeCommand(0xE8, {0x85, 0x00, 0x78});
    writeCommand(0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02});
    writeCommand(0xF7, {0x20});
    writeCommand(0xEA, {0x00, 0x00});
    writeCommand(0xC0, {0x23});
    writeCommand(0xC1, {0x10});
    writeCommand(0xC5, {0x3E, 0x28});
    writeCommand(0xC7, {0x86});
    writeCommand(0x36, {madctlForRotation(rotation_)});  // MADCTL
    writeCommand(0x3A, {0x55});                          // Pixel format: 16bpp
    writeCommand(0xB1, {0x00, 0x13});
    writeCommand(0xB6, {0x08, 0x82, 0x27});
    writeCommand(0xF2, {0x00});
    writeCommand(0x26, {0x01});
    writeCommand(0xE0, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37,
                        0x07, 0x10, 0x03, 0x0E, 0x09, 0x00});
    writeCommand(0xE1, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48,
                        0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F});

    writeCommand(0x11);  // Sleep out
    delay(120);
    writeCommand(0x29);  // Display on

    writeCommand(invertColor_ ? 0x21 : 0x20);

    return true;
  }

  void setRotation(Rotation rotation) override {
    if (rotation == Rotation::kNone) return;
    rotation_ = rotation;
    writeCommand(0x36, {madctlForRotation(rotation_)});
  }

  Rotation rotation() const { return rotation_; }

  /// See DisplayDriverSPI.h's ILI9341Driver::setInvertColor() - same
  /// panel quirk, same fix, just re-sent over this bus's begin() instead.
  void setInvertColor(bool invert) {
    invertColor_ = invert;
    writeCommand(invert ? 0x21 : 0x20);
  }

 protected:
  Rotation rotation_;
  size_t nativeWidth_, nativeHeight_;
  bool invertColor_ = false;

  static uint8_t madctlForRotation(Rotation rotation) {
    switch (rotation) {
      case Rotation::kLandscape:
        return 0x28;
      case Rotation::kPortraitFlipped:
        return 0x88;
      case Rotation::kLandscapeFlipped:
        return 0xE8;
      case Rotation::kNone:
      case Rotation::kPortrait:
      default:
        return 0x48;
    }
  }
};

/**
 * @brief Driver for ST7789 8-bit parallel display controller, driven via
 * ESP32's LCD_CAM/I80 or PARLIO peripheral (see
 * DisplayDriverParallel8ESP32).
 */
template <typename RGB_T = RGB565>
class ST7789Driver8080ESP32 : public DisplayDriverParallel8ESP32<RGB_T> {
 public:
  using DisplayDriverParallel8ESP32<RGB_T>::beginBus;
  using DisplayDriverParallel8ESP32<RGB_T>::writeCommand;

  ST7789Driver8080ESP32(int8_t d0, int8_t d1, int8_t d2, int8_t d3,
                        int8_t d4, int8_t d5, int8_t d6, int8_t d7,
                        int8_t wr, int8_t dc, int8_t cs, size_t width = 240,
                        size_t height = 320, uint32_t pclkHz = 20000000)
      : DisplayDriverParallel8ESP32<RGB_T>(d0, d1, d2, d3, d4, d5, d6, d7, wr,
                                           dc, cs, width, height, pclkHz) {}

  bool begin() override {
    if (!beginBus()) return false;
    writeCommand(0x01);  // SWRESET
    delay(150);
    writeCommand(0x11);  // Sleep out
    delay(120);
    writeCommand(0x3A, {0x55});  // Pixel format: 16bpp
    writeCommand(0x29);          // Display on
    return true;
  }
};

#endif  // TINYGPU_HAS_ESP_LCD_PARALLEL8

}  // namespace tinygpu
