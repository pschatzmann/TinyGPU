#pragma once
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Emulation/IQSPIBus.h"

#if defined(ESP32)
#include "driver/spi_master.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#define TINYGPU_HAS_ESP_LCD_QSPI 1
#endif

namespace tinygpu {

#ifdef TINYGPU_HAS_ESP_LCD_QSPI

/**
 * @brief Hardware-accelerated IQSPIBus backend for ESP32, built on top of
 * ESP-IDF's esp_lcd_new_panel_io_spi(quad_mode=1) - DMA-driven, with a
 * persistent scratch buffer and completion callback for writeColor() (see
 * that method's doc comment for why: esp_lcd_panel_io_tx_color() is
 * asynchronous). This is a straight extraction of what used to live
 * directly in DisplayDriverQSPI before the IQSPIBus split - behavior is
 * unchanged from before.
 */
class QSPIBusESP32 : public IQSPIBus {
 public:
  QSPIBusESP32(int8_t cs, int8_t sclk, int8_t d0, int8_t d1, int8_t d2,
              int8_t d3, size_t maxTransferBytes, uint32_t pclkHz,
              spi_host_device_t host)
      : cs_(cs),
        sclk_(sclk),
        d0_(d0),
        d1_(d1),
        d2_(d2),
        d3_(d3),
        maxTransferBytes_(maxTransferBytes),
        pclkHz_(pclkHz),
        host_(host) {}

  ~QSPIBusESP32() override {
    end();
    delete[] scratch_;
  }

  bool begin() override {
    spi_bus_config_t busCfg = {};
    busCfg.data0_io_num = d0_;
    busCfg.data1_io_num = d1_;
    busCfg.sclk_io_num = sclk_;
    busCfg.data2_io_num = d2_;
    busCfg.data3_io_num = d3_;
    busCfg.max_transfer_sz = static_cast<int>(maxTransferBytes_);

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
    ioCfg.on_color_trans_done = &QSPIBusESP32::onColorTransDone;
    ioCfg.user_ctx = this;

    return esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(host_),
                                    &ioCfg, &io_) == ESP_OK;
  }

  void end() override {
    if (io_ != nullptr) {
      esp_lcd_panel_io_del(io_);
      io_ = nullptr;
    }
  }

  bool writeCommand(uint8_t cmd, const uint8_t* param, size_t len) override {
    if (io_ == nullptr) return false;
    return esp_lcd_panel_io_tx_param(io_, qspiCmd(kOpcodeParam, cmd), param,
                                     len) == ESP_OK;
  }

  bool writeColor(uint8_t cmd, const uint8_t* data, size_t len) override {
    if (io_ == nullptr) return false;

    // Copied into a scratch buffer because esp_lcd_panel_io_tx_color() is
    // asynchronous (trans_queue_depth > 1 above): the caller's buffer
    // could be reused/freed before DMA finishes reading it. Blocking (via
    // on_color_trans_done) until the transfer completes turns this back
    // into the synchronous call IQSPIBus::writeColor() promises.
    if (scratchCapacity_ < len) {
      delete[] scratch_;
      scratch_ = new uint8_t[len];
      scratchCapacity_ = len;
    }
    memcpy(scratch_, data, len);

    colorTransPending_ = true;
    if (esp_lcd_panel_io_tx_color(io_, qspiCmd(kOpcodeColor, cmd), scratch_,
                                  len) != ESP_OK) {
      colorTransPending_ = false;
      return false;
    }
    while (colorTransPending_) {
      // busy-wait for the DMA transfer to finish
    }
    return true;
  }

 private:
  static constexpr uint8_t kOpcodeParam = 0x02;
  static constexpr uint8_t kOpcodeColor = 0x32;

  static int qspiCmd(uint8_t opcode, uint8_t cmd) {
    return (static_cast<int>(opcode) << 24) | (static_cast<int>(cmd) << 8);
  }

  // Runs in ISR context (per esp_lcd_panel_io_color_trans_done_cb_t's
  // contract) - keep this fast.
  static bool onColorTransDone(esp_lcd_panel_io_handle_t /*panel_io*/,
                               esp_lcd_panel_io_event_data_t* /*edata*/,
                               void* userCtx) {
    static_cast<QSPIBusESP32*>(userCtx)->colorTransPending_ = false;
    return false;  // no higher-priority task woken
  }

  int8_t cs_, sclk_, d0_, d1_, d2_, d3_;
  size_t maxTransferBytes_;
  uint32_t pclkHz_;
  spi_host_device_t host_;
  esp_lcd_panel_io_handle_t io_ = nullptr;
  uint8_t* scratch_ = nullptr;
  size_t scratchCapacity_ = 0;
  volatile bool colorTransPending_ = false;
};

#endif  // TINYGPU_HAS_ESP_LCD_QSPI

}  // namespace tinygpu
