#pragma once
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Emulation.h"

#if defined(ESP32)
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#define TINYGPU_HAS_ESP_LCD_QSPI 1
#else
#error
#endif

namespace tinygpu {

#ifdef TINYGPU_HAS_ESP_LCD_QSPI

/**
 * @brief Base driver for QSPI ("4-wire quad SPI") TFT panels: 1 clock, 1
 * chip-select, 4 data lines, no separate D/C pin. Used by a family of
 * compact panel controllers (NV3041A, ST77916, CO5300, SH8601,
 * AXS15231B, ...) that all speak a de-facto-standardized QSPI wire
 * protocol, wrapping each command in a fixed 32-bit frame: the top byte
 * is an opcode (0x02 = "write, parameter bytes follow over 1 line", 0x32
 * = "write, color/pixel data follows over 4 lines"), the next byte is
 * the actual MIPI-DCS-style command (0x11 sleep-out, 0x2A/0x2B/0x2C
 * column/row/RAM-write, 0x36 MADCTL, ...), and the low 16 bits go
 * unused. ESP-IDF's esp_lcd_new_panel_io_spi() builds that frame for
 * you when configured with quad_mode=1, dc_gpio_num=-1, and
 * lcd_cmd_bits=32 - this class only has to pass it the right `lcd_cmd`
 * values via qspiCmd(), not build the 32-bit frame by hand. Subclasses
 * implement begin() themselves (calling the inherited beginBus() for the
 * shared SPI-bus/panel-IO setup, then their own chip-specific init
 * register sequence), the same division of labor DisplayDriverSPI's
 * setupPinsAndReset() uses for its SPI-panel subclasses.
 */
template <typename RGB_T = RGB565>
class DisplayDriverQSPI : public DisplayDriver<RGB_T> {
 public:
  DisplayDriverQSPI(int8_t cs, int8_t sclk, int8_t d0, int8_t d1, int8_t d2,
                    int8_t d3, size_t width, size_t height,
                    uint32_t pclkHz = 40000000,
                    spi_host_device_t host = SPI3_HOST)
      : cs_(cs),
        sclk_(sclk),
        d0_(d0),
        d1_(d1),
        d2_(d2),
        d3_(d3),
        width_(width),
        height_(height),
        pclkHz_(pclkHz),
        host_(host) {}

  void end() override {
    if (io_ != nullptr) {
      esp_lcd_panel_io_del(io_);
      io_ = nullptr;
    }
  }

  ~DisplayDriverQSPI() override { delete[] scratch_; }

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

    // RGB_T (RGB565) values are stored in the surface already
    // byte-swapped to this panel's wire order (see RGB565.h), so no
    // per-pixel swap is needed here anymore. Still copied into a scratch
    // buffer because esp_lcd_panel_io_tx_color() is asynchronous
    // (trans_queue_depth > 1 above), so unlike DisplayDriverSPI's
    // writeData() this can't reuse/free a scratch buffer based on
    // assumptions about when a previous transfer happens to finish -
    // that raced with the DMA still reading the old buffer and crashed
    // on real hardware. Using one persistent, surface-sized scratch
    // buffer, and blocking (via the on_color_trans_done callback set up
    // in beginBus()) until the whole transfer completes before
    // returning.
    //
    // IMPORTANT: send this as ONE tx_color() call, not chunked at this
    // level. esp_lcd's own SPI backend (esp_lcd_panel_io_spi.c) already
    // splits large transfers into multiple queued DMA transactions
    // internally, sending the 0x2C RAM-write command only once up
    // front and keeping CS active across the internal chunks - that's
    // why the completion callback only fires after the true last
    // chunk. Chunking manually at this level by calling tx_color()
    // repeatedly re-sends the 0x2C command every time, which resets
    // this panel's write cursor back to the top-left of the address
    // window on each call - confirmed on real hardware to scramble the
    // image far worse than not chunking at all.
    const size_t n = surface.size();
    if (scratchCapacity_ < n) {
      delete[] scratch_;
      scratch_ = new uint8_t[n];
      scratchCapacity_ = n;
    }
    memcpy(scratch_, surface.data(), n);

    colorTransPending_ = true;
    if (esp_lcd_panel_io_tx_color(io_, qspiCmd(kOpcodeColor, kCmdRamWrite),
                                  scratch_, n) != ESP_OK) {
      colorTransPending_ = false;
      return false;
    }
    while (colorTransPending_) {
      // busy-wait for the DMA transfer to finish
    }
    return true;
  }

 protected:
  static constexpr uint8_t kOpcodeParam = 0x02;
  static constexpr uint8_t kOpcodeColor = 0x32;
  static constexpr uint8_t kCmdRamWrite = 0x2C;

  static int qspiCmd(uint8_t opcode, uint8_t cmd) {
    return (static_cast<int>(opcode) << 24) | (static_cast<int>(cmd) << 8);
  }

  /// Sets up the SPI bus (quad data lines) and the panel-IO layer that
  /// does the 32-bit command wrapping. Subclasses call this first thing
  /// in their begin(), then send their chip's init register sequence via
  /// writeCommand() before returning.
  bool beginBus() {
    spi_bus_config_t busCfg = {};
    busCfg.data0_io_num = d0_;
    busCfg.data1_io_num = d1_;
    busCfg.sclk_io_num = sclk_;
    busCfg.data2_io_num = d2_;
    busCfg.data3_io_num = d3_;
    busCfg.max_transfer_sz =
        static_cast<int>(width_ * height_ * sizeof(RGB_T));

    if (spi_bus_initialize(host_, &busCfg, SPI_DMA_CH_AUTO) != ESP_OK) {
      return false;
    }

    esp_lcd_panel_io_spi_config_t ioCfg = {};
    ioCfg.cs_gpio_num = cs_;
    ioCfg.dc_gpio_num = -1;
    ioCfg.spi_mode = 0;
    ioCfg.pclk_hz = pclkHz_;
    ioCfg.trans_queue_depth = 10;
    ioCfg.lcd_cmd_bits = 32;
    ioCfg.lcd_param_bits = 8;
    ioCfg.flags.quad_mode = 1;
    ioCfg.on_color_trans_done = &DisplayDriverQSPI<RGB_T>::onColorTransDone;
    ioCfg.user_ctx = this;

    return esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(host_),
                                    &ioCfg, &io_) == ESP_OK;
  }

  bool writeCommand(uint8_t cmd, const uint8_t* param = nullptr,
                    size_t len = 0) {
    return esp_lcd_panel_io_tx_param(io_, qspiCmd(kOpcodeParam, cmd), param,
                                     len) == ESP_OK;
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

  int8_t cs_, sclk_, d0_, d1_, d2_, d3_;
  size_t width_, height_;
  uint32_t pclkHz_;
  spi_host_device_t host_;
  esp_lcd_panel_io_handle_t io_ = nullptr;
  uint8_t* scratch_ = nullptr;
  size_t scratchCapacity_ = 0;
  volatile bool colorTransPending_ = false;

 private:
  // Runs in ISR context (per esp_lcd_panel_io_color_trans_done_cb_t's
  // contract) - keep this fast. Set as the panel-IO's on_color_trans_done
  // callback in beginBus(); see writeData() for why this exists.
  static bool onColorTransDone(esp_lcd_panel_io_handle_t /*panel_io*/,
                               esp_lcd_panel_io_event_data_t* /*edata*/,
                               void* userCtx) {
    static_cast<DisplayDriverQSPI<RGB_T>*>(userCtx)->colorTransPending_ = false;
    return false;  // no higher-priority task woken
  }
};

/**
 * @brief Driver for the New Vision NV3041A QSPI TFT controller (480x272
 * panels on Guition/Sunton JC4827W543-class ESP32-S3 boards).
 *
 * The full vendor register-unlock/gate/source-timing/gamma init
 * sequence, confirmed against real hardware (ported from
 * moononournation/Arduino_GFX's Arduino_NV3041A driver - an earlier
 * minimal sleep-out/pixel-format/MADCTL/display-on sequence produced a
 * rotated color channel mapping: red rendered as blue, green as red,
 * blue as green). Register 0x3A (pixel format) uses a chip-specific
 * encoding on this part, not the standard MIPI-DCS one: 0x01 selects
 * 16bpp RGB565 here, not the commonly-documented 0x55.
 */
template <typename RGB_T = RGB565>
class NV3041ADriver : public DisplayDriverQSPI<RGB_T> {
 public:
  using DisplayDriverQSPI<RGB_T>::beginBus;
  using DisplayDriverQSPI<RGB_T>::writeCommand;

  // 32MHz matches Arduino_GFX's Arduino_NV3041A driver, which documents
  // it as this chip's maximum supported speed and runs clean at it on
  // real hardware; this driver's original 40MHz default showed scattered
  // pixel corruption on large transfers, consistent with the panel/
  // wiring not reliably sustaining a faster clock.
  NV3041ADriver(int8_t cs, int8_t sclk, int8_t d0, int8_t d1, int8_t d2,
               int8_t d3, size_t width = 480, size_t height = 272,
               uint32_t pclkHz = 32000000)
      : DisplayDriverQSPI<RGB_T>(cs, sclk, d0, d1, d2, d3, width, height,
                                 pclkHz) {}

  bool begin() override {
    static_assert(sizeof(RGB_T) == 2,
                  "NV3041ADriver assumes a 16bpp RGB_T (RGB565)");

    if (!beginBus()) return false;

    for (size_t i = 0; i < sizeof(kInitOps) / sizeof(kInitOps[0]); ++i) {
      const uint8_t cmd = kInitOps[i][0];
      const uint8_t data = kInitOps[i][1];
      writeCommand(cmd, &data, 1);
      if (cmd == 0x11) delay(120);  // Sleep out
    }
    delay(100);  // after display on (0x29, the last op in kInitOps)

    // This panel is an IPS type: its default (uninverted) state and the
    // vendor gamma table above only produce correct colors with display
    // inversion turned ON - without this, colors come out wrong in a
    // gamma-dependent, non-uniform way (not a clean photographic
    // negative), which is why this was easy to miss.
    writeCommand(0x21);  // INVON

    return true;
  }

 private:
  // {command, data} pairs, in order. Ported verbatim from
  // moononournation/Arduino_GFX's nv3041a_init_operations - register
  // meanings beyond 0x3A/0x36/0x11/0x29 (gate/source timing, gamma,
  // power) are vendor-specific and undocumented beyond that source.
  static constexpr uint8_t kInitOps[][2] = {
      {0xff, 0xa5},  // register unlock
      {0x36, 0xc0},  // MADCTL
      {0x3A, 0x01},  // pixel format: 01=565, 00=666 (chip-specific encoding)
      {0x41, 0x03},  // 01=8bit, 03=16bit
      {0x44, 0x15}, {0x45, 0x15},  // VBP/VFP
      {0x7d, 0x03},
      {0xc1, 0xbb}, {0xc2, 0x05}, {0xc3, 0x10},
      {0xc6, 0x3e}, {0xc7, 0x25}, {0xc8, 0x11},
      {0x7a, 0x5f}, {0x6f, 0x44}, {0x78, 0x70},
      {0xc9, 0x00}, {0x67, 0x21},
      {0x51, 0x0a}, {0x52, 0x76}, {0x53, 0x0a}, {0x54, 0x76},  // gate timing
      {0x46, 0x0a}, {0x47, 0x2a}, {0x48, 0x0a}, {0x49, 0x1a},  // source timing
      {0x56, 0x43}, {0x57, 0x42}, {0x58, 0x3c}, {0x59, 0x64},
      {0x5a, 0x41}, {0x5b, 0x3c}, {0x5c, 0x02}, {0x5d, 0x3c},
      {0x5e, 0x1f}, {0x60, 0x80}, {0x61, 0x3f}, {0x62, 0x21},
      {0x63, 0x07}, {0x64, 0xe0}, {0x65, 0x02},
      {0xca, 0x20}, {0xcb, 0x52}, {0xcc, 0x10}, {0xcD, 0x42},
      {0xD0, 0x20}, {0xD1, 0x52}, {0xD2, 0x10}, {0xD3, 0x42},
      {0xD4, 0x0a}, {0xD5, 0x32},
      // gamma
      {0x80, 0x00}, {0xA0, 0x00}, {0x81, 0x07}, {0xA1, 0x06},
      {0x82, 0x02}, {0xA2, 0x01}, {0x86, 0x11}, {0xA6, 0x10},
      {0x87, 0x27}, {0xA7, 0x27}, {0x83, 0x37}, {0xA3, 0x37},
      {0x84, 0x35}, {0xA4, 0x35}, {0x85, 0x3f}, {0xA5, 0x3f},
      {0x88, 0x0b}, {0xA8, 0x0b}, {0x89, 0x14}, {0xA9, 0x14},
      {0x8a, 0x1a}, {0xAa, 0x1a}, {0x8b, 0x0a}, {0xAb, 0x0a},
      {0x8c, 0x14}, {0xAc, 0x08}, {0x8d, 0x17}, {0xAd, 0x07},
      {0x8e, 0x16}, {0xAe, 0x06}, {0x8f, 0x1B}, {0xAf, 0x07},
      {0x90, 0x04}, {0xB0, 0x04}, {0x91, 0x0A}, {0xB1, 0x0A},
      {0x92, 0x16}, {0xB2, 0x15},
      {0xff, 0x00},  // close register bank
      {0x11, 0x00},  // sleep out (triggers the 120ms delay above)
      {0x29, 0x00},  // display on
  };
};

#endif  // TINYGPU_HAS_ESP_LCD_QSPI

}  // namespace tinygpu
