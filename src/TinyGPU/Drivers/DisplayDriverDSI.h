#pragma once
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Color/RGB565.h"
#include "TinyGPU/Emulation.h"

// esp_lcd_mipi_dsi.h only exists in the ESP32-P4 variant of arduino-esp32's
// bundled ESP-IDF (MIPI-DSI is a P4-only peripheral) - __has_include lets
// this file compile as an empty no-op on every other target instead of
// hard-erroring, the same role DisplayDriverQSPI.h's `#if defined(ESP32)`
// plays for a family that's available everywhere ESP32 is.
#if __has_include("esp_lcd_mipi_dsi.h")
#include "driver/gpio.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_ldo_regulator.h"
#define TINYGPU_HAS_ESP_LCD_DSI 1
#endif

namespace tinygpu {

#ifdef TINYGPU_HAS_ESP_LCD_DSI

/**
 * @brief Base driver for MIPI-DSI panels driven through ESP-IDF's DBI
 * (command channel) + DPI (video-timing channel) split, the interface
 * ESP32-P4 exposes for DSI LCDs. Unlike DisplayDriverSPI/DisplayDriverQSPI,
 * a DPI panel has no address-window/RAMWR concept: `esp_lcd_new_panel_dpi()`
 * allocates a screen-sized, DMA-scanned framebuffer up front, and
 * `esp_lcd_panel_draw_bitmap()` writes straight into a rect of it - the
 * continuously-running DPI bridge just scans whatever's currently there.
 * `setAddressWindow()` is therefore a no-op here; writeData() addresses
 * directly via that rect instead.
 *
 * Only a single frame buffer (`num_fbs = 1`) is used, so a write landing
 * while that region happens to be mid-scan can tear - this driver doesn't
 * attempt the double-buffer-plus-dirty-rect scheme some vendor BSPs use to
 * avoid that (see guition-jc4880p4-bsp's board_p4.c for an example of that
 * approach); same tradeoff DisplayDriverSPI/QSPI already accept for a
 * torn/partial SPI transfer landing mid-refresh.
 *
 * Subclasses provide the panel-specific init register sequence (sent over
 * the DBI command channel via writeCommand()) and this panel's exact DSI
 * lane count/speed, DPI clock, and video timing - none of that is
 * standardized across MIPI-DSI panels the way the QSPI command-wrapping
 * convention is across QSPI panels, so unlike DisplayDriverQSPI there's no
 * shared default here beyond the plumbing.
 */
template <typename RGB_T = RGB565>
class DisplayDriverDSI : public DisplayDriver<RGB_T> {
 public:
  /**
   * @param rst panel reset GPIO, -1 if unused
   * @param backlight backlight GPIO (driven plain on/off - no PWM dimming),
   *   -1 if unused
   * @param width/height panel resolution in pixels
   * @param laneNum number of DSI data lanes
   * @param laneMbps per-lane bit rate in Mbps
   * @param dpiClockMhz DPI (pixel) clock frequency in MHz
   * @param phyLdoChan on-chip LDO channel powering the DSI PHY, -1 if the
   *   board supplies that rail some other way (no LDO acquire is done)
   * @param phyLdoMv LDO output voltage in mV, only used if phyLdoChan >= 0
   */
  DisplayDriverDSI(int8_t rst, int8_t backlight, size_t width, size_t height,
                   uint8_t laneNum, uint32_t laneMbps, uint32_t dpiClockMhz,
                   int8_t phyLdoChan = -1, uint16_t phyLdoMv = 2500)
      : rst_(rst),
        backlight_(backlight),
        width_(width),
        height_(height),
        laneNum_(laneNum),
        laneMbps_(laneMbps),
        dpiClockMhz_(dpiClockMhz),
        phyLdoChan_(phyLdoChan),
        phyLdoMv_(phyLdoMv) {}

  size_t width() const override { return width_; }
  size_t height() const override { return height_; }

  void end() override {
    if (panel_ != nullptr) {
      esp_lcd_panel_del(panel_);
      panel_ = nullptr;
    }
    if (dbiIo_ != nullptr) {
      esp_lcd_panel_io_del(dbiIo_);
      dbiIo_ = nullptr;
    }
    if (dsiBus_ != nullptr) {
      esp_lcd_del_dsi_bus(dsiBus_);
      dsiBus_ = nullptr;
    }
    if (ldoHandle_ != nullptr) {
      esp_ldo_release_channel(ldoHandle_);
      ldoHandle_ = nullptr;
    }
  }

  ~DisplayDriverDSI() override {
    end();
    delete[] scratch_;
  }

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    static_assert(sizeof(RGB_T) == 2,
                  "DisplayDriverDSI assumes a 16bpp RGB_T (RGB565)");
    if (panel_ == nullptr) return false;

    // RGB_T (RGB565) values are stored byte-swapped to SPI/QSPI wire order
    // (see RGB565.h) - a DPI framebuffer instead wants the conventional
    // (native) RGB565 bit layout, so swap back into a scratch buffer
    // before handing it to esp_lcd, same as DisplayDriverSDL does and for
    // the same reason (this is a plain memory framebuffer, not a
    // command-driven SPI panel).
    const size_t n = surface.size();
    if (scratchCapacity_ < n) {
      delete[] scratch_;
      scratch_ = new uint8_t[n];
      scratchCapacity_ = n;
    }
    const uint16_t* src = reinterpret_cast<const uint16_t*>(surface.data());
    uint16_t* dst = reinterpret_cast<uint16_t*>(scratch_);
    const size_t pixelCount = n / sizeof(RGB_T);
    for (size_t i = 0; i < pixelCount; ++i) {
      dst[i] = RGB565::swapBytes(src[i]);
    }

    return esp_lcd_panel_draw_bitmap(panel_, x, y, x + surface.width(),
                                     y + surface.height(),
                                     scratch_) == ESP_OK;
  }

 protected:
  /// Sets up the DSI-PHY power rail (if phyLdoChan >= 0), the DSI bus, the
  /// DBI command-IO layer, and the DPI video panel, then pulses reset.
  /// Subclasses call this first thing in their begin(), then send their
  /// panel's init register sequence via writeCommand() before calling
  /// finishInit() - the same beginBus()-then-init-sequence division of
  /// labor DisplayDriverQSPI's subclasses use.
  bool beginBus() {
    if (phyLdoChan_ >= 0) {
      esp_ldo_channel_config_t ldoCfg = {};
      ldoCfg.chan_id = phyLdoChan_;
      ldoCfg.voltage_mv = phyLdoMv_;
      if (esp_ldo_acquire_channel(&ldoCfg, &ldoHandle_) != ESP_OK) {
        return false;
      }
    }

    esp_lcd_dsi_bus_config_t busCfg = {};
    busCfg.bus_id = 0;
    busCfg.num_data_lanes = laneNum_;
    busCfg.phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
    busCfg.lane_bit_rate_mbps = static_cast<float>(laneMbps_);
    if (esp_lcd_new_dsi_bus(&busCfg, &dsiBus_) != ESP_OK) return false;

    esp_lcd_dbi_io_config_t dbiCfg = {};
    dbiCfg.virtual_channel = 0;
    dbiCfg.lcd_cmd_bits = 8;
    dbiCfg.lcd_param_bits = 8;
    if (esp_lcd_new_panel_io_dbi(dsiBus_, &dbiCfg, &dbiIo_) != ESP_OK) {
      return false;
    }

    esp_lcd_dpi_panel_config_t dpiCfg = {};
    dpiCfg.virtual_channel = 0;
    dpiCfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
    dpiCfg.dpi_clock_freq_mhz = static_cast<float>(dpiClockMhz_);
    dpiCfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
    dpiCfg.in_color_format = LCD_COLOR_FMT_RGB565;
    dpiCfg.out_color_format = LCD_COLOR_FMT_RGB565;
    dpiCfg.num_fbs = 1;
    dpiCfg.video_timing.h_size = width_;
    dpiCfg.video_timing.v_size = height_;
    dpiCfg.video_timing.hsync_pulse_width = hsyncPulseWidth_;
    dpiCfg.video_timing.hsync_back_porch = hsyncBackPorch_;
    dpiCfg.video_timing.hsync_front_porch = hsyncFrontPorch_;
    dpiCfg.video_timing.vsync_pulse_width = vsyncPulseWidth_;
    dpiCfg.video_timing.vsync_back_porch = vsyncBackPorch_;
    dpiCfg.video_timing.vsync_front_porch = vsyncFrontPorch_;
    dpiCfg.flags.use_dma2d = 1;
    if (esp_lcd_new_panel_dpi(dsiBus_, &dpiCfg, &panel_) != ESP_OK) {
      return false;
    }

    resetPanel();
    return true;
  }

  /// Sets the panel's DSI video timing (porches/sync widths, in pixels/
  /// lines). Must be called before beginBus(); every panel documents its
  /// own values, there is no cross-panel default worth assuming.
  void setVideoTiming(uint16_t hsyncPulseWidth, uint16_t hsyncBackPorch,
                      uint16_t hsyncFrontPorch, uint16_t vsyncPulseWidth,
                      uint16_t vsyncBackPorch, uint16_t vsyncFrontPorch) {
    hsyncPulseWidth_ = hsyncPulseWidth;
    hsyncBackPorch_ = hsyncBackPorch;
    hsyncFrontPorch_ = hsyncFrontPorch;
    vsyncPulseWidth_ = vsyncPulseWidth;
    vsyncBackPorch_ = vsyncBackPorch;
    vsyncFrontPorch_ = vsyncFrontPorch;
  }

  void resetPanel() {
    if (rst_ < 0) return;
    pinMode(rst_, OUTPUT);
    digitalWrite(rst_, LOW);
    delay(20);
    digitalWrite(rst_, HIGH);
    delay(120);
  }

  /// Sends one command + parameter bytes over the DBI channel - the DSI
  /// equivalent of DisplayDriverQSPI's writeCommand(), addressing the
  /// panel's MIPI-DCS/vendor register set rather than wrapping a 32-bit
  /// QSPI frame.
  bool writeCommand(uint8_t cmd, const uint8_t* param = nullptr,
                    size_t len = 0) {
    return esp_lcd_panel_io_tx_param(dbiIo_, cmd, param, len) == ESP_OK;
  }

  /// Finalizes the panel after the init register sequence has been sent -
  /// esp_lcd's DPI backend starts scanning out once this returns.
  bool finishInit() { return esp_lcd_panel_init(panel_) == ESP_OK; }

  void setBacklight(bool on) {
    if (backlight_ < 0) return;
    pinMode(backlight_, OUTPUT);
    digitalWrite(backlight_, on ? HIGH : LOW);
  }

  bool setAddressWindow(size_t /*x*/, size_t /*y*/, size_t /*w*/,
                        size_t /*h*/) override {
    return true;  // no-op - see class comment
  }

  int8_t rst_, backlight_;
  size_t width_, height_;
  uint8_t laneNum_;
  uint32_t laneMbps_;
  uint32_t dpiClockMhz_;
  int8_t phyLdoChan_;
  uint16_t phyLdoMv_;
  uint16_t hsyncPulseWidth_ = 10, hsyncBackPorch_ = 10, hsyncFrontPorch_ = 10;
  uint16_t vsyncPulseWidth_ = 2, vsyncBackPorch_ = 10, vsyncFrontPorch_ = 10;
  esp_ldo_channel_handle_t ldoHandle_ = nullptr;
  esp_lcd_dsi_bus_handle_t dsiBus_ = nullptr;
  esp_lcd_panel_io_handle_t dbiIo_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;

 private:
  uint8_t* scratch_ = nullptr;
  size_t scratchCapacity_ = 0;
};

/**
 * @brief Driver for the Sitronix ST7701S MIPI-DSI TFT controller, as wired
 * on the Guition JC4880P443C_I_W (ESP32-P4, 480x800 portrait, 4.3").
 *
 * Init register sequence, DSI lane config (2 lanes @ 500Mbps), DPI clock
 * (34MHz) and video timing all transcribed verbatim from
 * guition-jc4880p4-bsp's board_p4_st7701_init.h / board_p4.c
 * (https://github.com/ultramcu/guition-jc4880p4-bsp), which documents them
 * as VERIFIED-ON-HARDWARE on this exact board - this class only re-expresses
 * that same recipe as a TinyGPU DisplayDriver instead of a bespoke BSP
 * function, it does not add or change any of the underlying values. As with
 * every other example in this repo's esp32-p4-480x800-guition-mipi-dsi-touch-display
 * board directory, this file itself has not been run on real hardware.
 *
 * Register meanings beyond 0x11 (SLPOUT) / 0x29 (DISPON) are vendor-specific
 * (ST7701 command-bank-switched extension registers) and undocumented
 * beyond that source.
 */
template <typename RGB_T = RGB565>
class ST7701Driver : public DisplayDriverDSI<RGB_T> {
 public:
  using DisplayDriverDSI<RGB_T>::beginBus;
  using DisplayDriverDSI<RGB_T>::writeCommand;
  using DisplayDriverDSI<RGB_T>::finishInit;
  using DisplayDriverDSI<RGB_T>::setBacklight;
  using DisplayDriverDSI<RGB_T>::setVideoTiming;

  ST7701Driver(int8_t rst, int8_t backlight, size_t width = 480,
              size_t height = 800, int8_t phyLdoChan = 3,
              uint16_t phyLdoMv = 2500)
      : DisplayDriverDSI<RGB_T>(rst, backlight, width, height,
                                /*laneNum=*/2, /*laneMbps=*/500,
                                /*dpiClockMhz=*/34, phyLdoChan, phyLdoMv) {}

  bool begin() override {
    static_assert(sizeof(RGB_T) == 2, "ST7701Driver assumes a 16bpp RGB_T (RGB565)");

    setVideoTiming(/*hsyncPulseWidth=*/12, /*hsyncBackPorch=*/42,
                   /*hsyncFrontPorch=*/42, /*vsyncPulseWidth=*/2,
                   /*vsyncBackPorch=*/8, /*vsyncFrontPorch=*/166);

    if (!beginBus()) return false;

    for (size_t i = 0; i < sizeof(kInitOps) / sizeof(kInitOps[0]); ++i) {
      const InitOp& op = kInitOps[i];
      writeCommand(op.cmd, op.data, op.len);
      if (op.delayMs) delay(op.delayMs);
    }

    if (!finishInit()) return false;
    setBacklight(true);
    return true;
  }

 private:
  struct InitOp {
    uint8_t cmd;
    const uint8_t* data;
    size_t len;
    uint16_t delayMs;
  };

  // {cmd, &params, param byte count, post-delay ms}, verbatim from
  // guition-jc4880p4-bsp's board_p4_panel_init_cmds[] - see class comment.
  static constexpr uint8_t kFf13[] = {0x77, 0x01, 0x00, 0x00, 0x13};
  static constexpr uint8_t kEf08[] = {0x08};
  static constexpr uint8_t kFf10[] = {0x77, 0x01, 0x00, 0x00, 0x10};
  static constexpr uint8_t kC0[] = {0x63, 0x00};
  static constexpr uint8_t kC1[] = {0x0D, 0x02};
  static constexpr uint8_t kC2[] = {0x10, 0x08};
  static constexpr uint8_t kCc[] = {0x10};
  static constexpr uint8_t kB0Gamma[] = {0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07,
                                         0x0C, 0x09, 0x09, 0x28, 0x06, 0xD4,
                                         0x13, 0x69, 0x2B, 0x71};
  static constexpr uint8_t kB1Gamma[] = {0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06,
                                         0x0A, 0x08, 0x08, 0x25, 0x03, 0xD3,
                                         0x12, 0x66, 0x6A, 0x0D};
  static constexpr uint8_t kFf11[] = {0x77, 0x01, 0x00, 0x00, 0x11};
  static constexpr uint8_t kB0[] = {0x5D};
  static constexpr uint8_t kB1[] = {0x58};
  static constexpr uint8_t kB2[] = {0x87};
  static constexpr uint8_t kB3[] = {0x80};
  static constexpr uint8_t kB5[] = {0x4E};
  static constexpr uint8_t kB7[] = {0x85};
  static constexpr uint8_t kB8[] = {0x21};
  static constexpr uint8_t kB9[] = {0x10, 0x1F};
  static constexpr uint8_t kBb[] = {0x03};
  static constexpr uint8_t kBc[] = {0x00};
  static constexpr uint8_t kC178[] = {0x78};
  static constexpr uint8_t kC278[] = {0x78};
  static constexpr uint8_t kD0[] = {0x88};
  static constexpr uint8_t kE0[] = {0x00, 0x3A, 0x02};
  static constexpr uint8_t kE1[] = {0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0,
                                    0x00, 0xA0, 0x00, 0x40, 0x40};
  static constexpr uint8_t kE2[] = {0x30, 0x00, 0x40, 0x40, 0x32, 0xA0,
                                    0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00};
  static constexpr uint8_t kE3[] = {0x00, 0x00, 0x33, 0x33};
  static constexpr uint8_t kE4[] = {0x44, 0x44};
  static constexpr uint8_t kE5[] = {0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30,
                                    0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0,
                                    0x07, 0x2C, 0xA0, 0xA0};
  static constexpr uint8_t kE6[] = {0x00, 0x00, 0x33, 0x33};
  static constexpr uint8_t kE7[] = {0x44, 0x44};
  static constexpr uint8_t kE8[] = {0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F,
                                    0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0,
                                    0x06, 0x2B, 0xA0, 0xA0};
  static constexpr uint8_t kEb[] = {0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00};
  static constexpr uint8_t kEc[] = {0x08, 0x01};
  static constexpr uint8_t kEd[] = {0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65,
                                    0x4A, 0x89, 0xB2, 0x0B};
  static constexpr uint8_t kEf[] = {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54};
  static constexpr uint8_t kFf00[] = {0x77, 0x01, 0x00, 0x00, 0x00};
  static constexpr uint8_t kNop[] = {0x00};

  static constexpr InitOp kInitOps[] = {
      {0xFF, kFf13, sizeof(kFf13), 0},
      {0xEF, kEf08, sizeof(kEf08), 0},
      {0xFF, kFf10, sizeof(kFf10), 0},
      {0xC0, kC0, sizeof(kC0), 0},
      {0xC1, kC1, sizeof(kC1), 0},
      {0xC2, kC2, sizeof(kC2), 0},
      {0xCC, kCc, sizeof(kCc), 0},
      {0xB0, kB0Gamma, sizeof(kB0Gamma), 0},
      {0xB1, kB1Gamma, sizeof(kB1Gamma), 0},
      {0xFF, kFf11, sizeof(kFf11), 0},
      {0xB0, kB0, sizeof(kB0), 0},
      {0xB1, kB1, sizeof(kB1), 0},
      {0xB2, kB2, sizeof(kB2), 0},
      {0xB3, kB3, sizeof(kB3), 0},
      {0xB5, kB5, sizeof(kB5), 0},
      {0xB7, kB7, sizeof(kB7), 0},
      {0xB8, kB8, sizeof(kB8), 0},
      {0xB9, kB9, sizeof(kB9), 0},
      {0xBB, kBb, sizeof(kBb), 0},
      {0xBC, kBc, sizeof(kBc), 0},
      {0xC1, kC178, sizeof(kC178), 0},
      {0xC2, kC278, sizeof(kC278), 0},
      {0xD0, kD0, sizeof(kD0), 0},
      {0xE0, kE0, sizeof(kE0), 0},
      {0xE1, kE1, sizeof(kE1), 0},
      {0xE2, kE2, sizeof(kE2), 0},
      {0xE3, kE3, sizeof(kE3), 0},
      {0xE4, kE4, sizeof(kE4), 0},
      {0xE5, kE5, sizeof(kE5), 0},
      {0xE6, kE6, sizeof(kE6), 0},
      {0xE7, kE7, sizeof(kE7), 0},
      {0xE8, kE8, sizeof(kE8), 0},
      {0xEB, kEb, sizeof(kEb), 0},
      {0xEC, kEc, sizeof(kEc), 0},
      {0xED, kEd, sizeof(kEd), 0},
      {0xEF, kEf, sizeof(kEf), 0},
      {0xFF, kFf00, sizeof(kFf00), 0},
      {0x11, kNop, sizeof(kNop), 120},  // SLPOUT
      {0x29, kNop, sizeof(kNop), 20},   // DISPON
  };
};

#endif  // TINYGPU_HAS_ESP_LCD_DSI

}  // namespace tinygpu
