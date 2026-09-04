#pragma once
#include <stdint.h>

#include "TinyGPU/Abstractions/IDSIBus.h"

// esp_lcd_mipi_dsi.h only exists in the ESP32-P4 variant of arduino-esp32's
// bundled ESP-IDF (MIPI-DSI is a P4-only peripheral) - __has_include lets
// this file compile as an empty no-op on every other target instead of
// hard-erroring.
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
 * @brief IDSIBus backend for ESP32-P4, built on ESP-IDF's DBI (command
 * channel) + DPI (video-timing channel) split - the interface ESP32-P4
 * exposes for DSI LCDs. A DPI panel has no address-window/RAMWR concept:
 * `esp_lcd_new_panel_dpi()` allocates a screen-sized, DMA-scanned
 * framebuffer up front, and `esp_lcd_panel_draw_bitmap()` writes straight
 * into a rect of it - the continuously-running DPI bridge just scans
 * whatever's currently there.
 *
 * Only a single frame buffer (`num_fbs = 1`) is used, so a write landing
 * while that region happens to be mid-scan can tear - this driver doesn't
 * attempt the double-buffer-plus-dirty-rect scheme some vendor BSPs use to
 * avoid that; same tradeoff DisplayDriverSPI/QSPI already accept for a
 * torn/partial transfer landing mid-refresh.
 *
 * This is a straight extraction of what used to live directly in
 * DisplayDriverDSI before the IDSIBus split - behavior is unchanged.
 */
class DSIBusESP32 : public IDSIBus {
 public:
  /**
   * @param rst panel reset GPIO, -1 if unused
   * @param width/height panel resolution in pixels
   * @param laneNum number of DSI data lanes
   * @param laneMbps per-lane bit rate in Mbps
   * @param dpiClockMhz DPI (pixel) clock frequency in MHz
   * @param phyLdoChan on-chip LDO channel powering the DSI PHY, -1 if the
   *   board supplies that rail some other way (no LDO acquire is done)
   * @param phyLdoMv LDO output voltage in mV, only used if phyLdoChan >= 0
   */
  DSIBusESP32(int8_t rst, size_t width, size_t height, uint8_t laneNum,
             uint32_t laneMbps, uint32_t dpiClockMhz, int8_t phyLdoChan,
             uint16_t phyLdoMv)
      : rst_(rst),
        width_(width),
        height_(height),
        laneNum_(laneNum),
        laneMbps_(laneMbps),
        dpiClockMhz_(dpiClockMhz),
        phyLdoChan_(phyLdoChan),
        phyLdoMv_(phyLdoMv) {}

  ~DSIBusESP32() override { end(); }

  /// Sets the panel's DSI video timing (porches/sync widths, in pixels/
  /// lines). Must be called before begin(); every panel documents its own
  /// values, there is no cross-panel default worth assuming.
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

  bool begin() override {
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

  bool writeCommand(uint8_t cmd, const uint8_t* param, size_t len) override {
    return esp_lcd_panel_io_tx_param(dbiIo_, cmd, param, len) == ESP_OK;
  }

  /// esp_lcd's DPI backend starts scanning out once this returns.
  bool finishInit() override { return esp_lcd_panel_init(panel_) == ESP_OK; }

  bool drawBitmap(size_t x0, size_t y0, size_t x1, size_t y1,
                  const uint8_t* rgb565) override {
    if (panel_ == nullptr) return false;
    return esp_lcd_panel_draw_bitmap(panel_, x0, y0, x1, y1,
                                     rgb565) == ESP_OK;
  }

 private:
  void resetPanel() {
    if (rst_ < 0) return;
    pinMode(rst_, OUTPUT);
    digitalWrite(rst_, LOW);
    delay(20);
    digitalWrite(rst_, HIGH);
    delay(120);
  }

  int8_t rst_;
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
};

#endif  // TINYGPU_HAS_ESP_LCD_DSI

}  // namespace tinygpu
