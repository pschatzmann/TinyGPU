#pragma once
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Abstractions/IDSIBus.h"

// Only STM32 parts with a DSI host (STM32F469/F479, STM32F769/F779,
// STM32H747/H757, STM32U5x9, ...) ship stm32yyxx_hal_dsi.h in STM32duino's
// core - __has_include lets this file compile as an empty no-op on every
// other STM32 target (and on non-STM32 platforms) instead of hard-erroring,
// the same role DSIBusESP32.h's __has_include("esp_lcd_mipi_dsi.h") plays
// for the ESP32-P4-only DSI peripheral.
#if defined(STM32) || defined(ARDUINO_ARCH_STM32)
#if __has_include("stm32yyxx_hal_dsi.h") && __has_include("stm32yyxx_hal_ltdc.h")
#include "stm32yyxx_hal_dsi.h"
#include "stm32yyxx_hal_ltdc.h"
#define TINYGPU_HAS_STM32_DSI 1
#endif
#endif

namespace tinygpu {

#ifdef TINYGPU_HAS_STM32_DSI

/**
 * @brief IDSIBus backend for STM32 parts with a DSI host + LTDC (F469/
 * F479, F769/F779, H747/H757, U5x9, ...), built on STM32Cube HAL's
 * HAL_DSI_* (command channel, adapted/video-mode DSI wrapper) and
 * HAL_LTDC_* (the pixel-timing generator/framebuffer scanner DSI sits in
 * front of on these parts - conceptually the same role ESP32-P4's DPI
 * peripheral plays for DSIBusESP32).
 *
 * UNVERIFIED ON REAL HARDWARE - unlike DSIBusESP32 (whose ESP32-P4 DPI
 * path is exercised by boards this library ships examples for), no
 * STM32-DSI board has been used to test this class. The DSI-video-timing
 * math (HSA/HBP/HLINE expressed in DSI lane-byte-clock cycles, derived
 * from the pixel-domain porches via the standard
 * `<pixels> * laneByteClk_kHz / dpiClock_kHz` conversion) and the LTDC
 * accumulated-timing fields follow the pattern used throughout ST's own
 * DSI panel BSPs (e.g. the OTM8009A driver shipped with the
 * STM32F769I-Discovery/STM32H747I-Discovery BSPs) - treat it as a
 * documented starting point for board bring-up, not a guarantee.
 *
 * Deliberately NOT in scope here: the DSI PHY PLL (dsiPllInit - NDIV/IDF/
 * ODF) and the LTDC pixel clock (normally routed off RCC PLL3 on these
 * parts). Both depend on the board's external oscillator and are
 * ordinarily produced by STM32CubeMX's generated SystemClock_Config() -
 * this class takes the already-computed DSI_PLLInitTypeDef and LTDC
 * pixel clock (in kHz) as constructor inputs rather than guessing at
 * divisors that would silently be wrong for any board other than the one
 * they were tuned for.
 *
 * Only a single framebuffer is used (no double-buffering), same tradeoff
 * DSIBusESP32 accepts - see that class's doc comment.
 */
class DSIBusSTM32 : public IDSIBus {
 public:
  /**
   * @param rst panel reset GPIO, -1 if unused
   * @param width/height panel resolution in pixels
   * @param laneNum number of DSI data lanes (1 or 2)
   * @param laneMbps per-lane bit rate in Mbps (used only for the DSI
   *   video-timing conversion below - does NOT configure the DSI PHY PLL,
   *   see pllInit)
   * @param dpiClockKHz pixel clock in kHz that the caller has already
   *   arranged to feed the LTDC (typically via RCC PLL3), matching what
   *   `pllInit` was computed for
   * @param pllInit pre-computed DSI PHY PLL config (NDIV/IDF/ODF) for
   *   this board's external oscillator - normally CubeMX-generated
   * @param framebuffer caller-owned RGB565 framebuffer,
   *   width*height*2 bytes, placed by the caller in whatever RAM region
   *   the board's LTDC/DMA2D can actually read from (not every STM32 RAM
   *   region is - AXI SRAM/SDRAM are typical, not all of DTCM/ITCM)
   */
  DSIBusSTM32(int8_t rst, size_t width, size_t height, uint8_t laneNum,
             uint32_t laneMbps, uint32_t dpiClockKHz,
             const DSI_PLLInitTypeDef& pllInit, uint16_t* framebuffer)
      : rst_(rst),
        width_(width),
        height_(height),
        laneNum_(laneNum),
        laneMbps_(laneMbps),
        dpiClockKHz_(dpiClockKHz),
        pllInit_(pllInit),
        framebuffer_(framebuffer) {}

  /// Sets the panel's video timing (porches/sync widths, in pixels/
  /// lines). Must be called before begin(); every panel documents its
  /// own values, there is no cross-panel default worth assuming.
  void setVideoTiming(uint16_t hsyncPulseWidth, uint16_t hsyncBackPorch,
                      uint16_t hsyncFrontPorch, uint16_t vsyncPulseWidth,
                      uint16_t vsyncBackPorch, uint16_t vsyncFrontPorch) {
    hsa_ = hsyncPulseWidth;
    hbp_ = hsyncBackPorch;
    hfp_ = hsyncFrontPorch;
    vsa_ = vsyncPulseWidth;
    vbp_ = vsyncBackPorch;
    vfp_ = vsyncFrontPorch;
  }

  bool begin() override {
    resetPanel();

    hdsi_.Instance = DSI;
    hdsi_.Init.AutomaticClockLaneControl = DSI_AUTO_CLK_LANE_CTRL_DISABLE;
    hdsi_.Init.TXEscapeCkdiv = 4;
    hdsi_.Init.NumberOfLanes =
        laneNum_ >= 2 ? DSI_TWO_DATA_LANES : DSI_ONE_DATA_LANE;
    DSI_PLLInitTypeDef pllInit = pllInit_;
    if (HAL_DSI_Init(&hdsi_, &pllInit) != HAL_OK) return false;

    DSI_PHY_TimerTypeDef phyTimings = {};
    phyTimings.ClockLaneHS2LPTime = 35;
    phyTimings.ClockLaneLP2HSTime = 35;
    phyTimings.DataLaneHS2LPTime = 35;
    phyTimings.DataLaneLP2HSTime = 35;
    phyTimings.DataLaneMaxReadTime = 0;
    phyTimings.StopWaitTime = 10;
    if (HAL_DSI_ConfigPhyTimer(&hdsi_, &phyTimings) != HAL_OK) return false;

    DSI_HOST_TimeoutTypeDef hostTimeouts = {};
    hostTimeouts.TimeoutCkdiv = 1;
    hostTimeouts.HighSpeedTransmissionTimeout = 0;
    hostTimeouts.LowPowerReceptionTimeout = 0;
    hostTimeouts.HighSpeedReadTimeout = 0;
    hostTimeouts.LowPowerReadTimeout = 0;
    hostTimeouts.HighSpeedWriteTimeout = 0;
    hostTimeouts.HighSpeedWritePrespMode = DSI_HS_PM_DISABLE;
    hostTimeouts.LowPowerWriteTimeout = 0;
    hostTimeouts.BTATimeout = 0;
    if (HAL_DSI_ConfigHostTimeouts(&hdsi_, &hostTimeouts) != HAL_OK) {
      return false;
    }

    if (HAL_DSI_ConfigFlowControl(&hdsi_, DSI_FLOW_CONTROL_BTA) != HAL_OK) {
      return false;
    }
    if (HAL_DSI_SetGenericVCID(&hdsi_, 0) != HAL_OK) return false;

    // LP command mode only for now (writeCommand()) - video mode/LTDC are
    // brought up afterwards in finishInit(), once the panel's init
    // sequence has been sent, mirroring DisplayDriverDSI's beginBus()-
    // then-init-sequence-then-finishInit() split for DSIBusESP32.
    DSI_CmdCfgTypeDef cmdCfg = {};
    cmdCfg.VirtualChannelID = 0;
    cmdCfg.ColorCoding = DSI_RGB565;
    cmdCfg.CommandSize = static_cast<uint16_t>(width_);
    cmdCfg.TearingEffectSource = DSI_TE_DSILINK;
    cmdCfg.TearingEffectPolarity = DSI_TE_RISING_EDGE;
    cmdCfg.VSyncPol = DSI_VSYNC_FALLING;
    cmdCfg.AutomaticRefresh = DSI_AR_DISABLE;
    cmdCfg.TEAcknowledgeRequest = DSI_TE_ACKNOWLEDGE_ENABLE;
    if (HAL_DSI_ConfigAdaptedCommandMode(&hdsi_, &cmdCfg) != HAL_OK) {
      return false;
    }

    if (HAL_DSI_Start(&hdsi_) != HAL_OK) return false;
    return true;
  }

  void end() override {
    HAL_DSI_Stop(&hdsi_);
    HAL_DSI_DeInit(&hdsi_);
    HAL_LTDC_DeInit(&hltdc_);
  }

  bool writeCommand(uint8_t cmd, const uint8_t* param, size_t len) override {
    if (len == 0) {
      return HAL_DSI_ShortWrite(&hdsi_, 0, DSI_DCS_SHORT_PKT_WRITE_P0, cmd,
                                0) == HAL_OK;
    }
    if (len == 1) {
      return HAL_DSI_ShortWrite(&hdsi_, 0, DSI_DCS_SHORT_PKT_WRITE_P1, cmd,
                                param[0]) == HAL_OK;
    }
    // HAL_DSI_LongWrite's payload buffer is {cmd, param[0], param[1], ...}
    // - build it in scratch_ since `param` is caller-owned and const.
    if (scratchCapacity_ < len + 1) {
      delete[] scratch_;
      scratch_ = new uint8_t[len + 1];
      scratchCapacity_ = len + 1;
    }
    scratch_[0] = cmd;
    memcpy(scratch_ + 1, param, len);
    return HAL_DSI_LongWrite(&hdsi_, 0, DSI_DCS_LONG_PKT_WRITE,
                             static_cast<uint32_t>(len + 1), cmd,
                             scratch_) == HAL_OK;
  }

  /// Configures and starts the DSI video wrapper + LTDC framebuffer scan-
  /// out - the panel is expected to be fully initialized (sleep-out,
  /// display-on, ...) by this point via writeCommand().
  bool finishInit() override {
    // DSI video timing, converted from the pixel-clock domain into the
    // DSI lane-byte-clock domain - see class doc comment.
    const uint32_t laneByteClkKHz = (laneMbps_ * 1000) / 8;
    DSI_VidCfgTypeDef vidCfg = {};
    vidCfg.VirtualChannelID = 0;
    vidCfg.ColorCoding = DSI_RGB565;
    vidCfg.Mode = DSI_VID_MODE_BURST;
    vidCfg.PacketSize = static_cast<uint32_t>(width_);
    vidCfg.NumberOfChunks = 0;
    vidCfg.NullPacketSize = 0xFFF;
    vidCfg.HSPolarity = DSI_HSYNC_ACTIVE_HIGH;
    vidCfg.VSPolarity = DSI_VSYNC_ACTIVE_HIGH;
    vidCfg.DEPolarity = DSI_DATA_ENABLE_ACTIVE_HIGH;
    vidCfg.HorizontalSyncActive = (hsa_ * laneByteClkKHz) / dpiClockKHz_;
    vidCfg.HorizontalBackPorch = (hbp_ * laneByteClkKHz) / dpiClockKHz_;
    vidCfg.HorizontalLine =
        ((width_ + hsa_ + hbp_ + hfp_) * laneByteClkKHz) / dpiClockKHz_;
    vidCfg.VerticalSyncActive = vsa_;
    vidCfg.VerticalBackPorch = vbp_;
    vidCfg.VerticalFrontPorch = vfp_;
    vidCfg.VerticalActive = static_cast<uint32_t>(height_);
    vidCfg.LPCommandEnable = DSI_LP_COMMAND_ENABLE;
    vidCfg.LPLargestPacketSize = 16;
    vidCfg.LPVACTLargestPacketSize = 0;
    vidCfg.LPHorizontalFrontPorchEnable = DSI_LP_HFP_ENABLE;
    vidCfg.LPHorizontalBackPorchEnable = DSI_LP_HBP_ENABLE;
    vidCfg.LPVerticalActiveEnable = DSI_LP_VACT_ENABLE;
    vidCfg.LPVerticalFrontPorchEnable = DSI_LP_VFP_ENABLE;
    vidCfg.LPVerticalBackPorchEnable = DSI_LP_VBP_ENABLE;
    vidCfg.LPVerticalSyncActiveEnable = DSI_LP_VSYNC_ENABLE;
    vidCfg.FrameBTAAcknowledgeEnable = DSI_FBTAA_DISABLE;
    if (HAL_DSI_ConfigVideoMode(&hdsi_, &vidCfg) != HAL_OK) return false;

    hltdc_.Instance = LTDC;
    hltdc_.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    hltdc_.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    hltdc_.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    hltdc_.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    hltdc_.Init.HorizontalSync = static_cast<uint32_t>(hsa_ - 1);
    hltdc_.Init.VerticalSync = static_cast<uint32_t>(vsa_ - 1);
    hltdc_.Init.AccumulatedHBP = static_cast<uint32_t>(hsa_ + hbp_ - 1);
    hltdc_.Init.AccumulatedVBP = static_cast<uint32_t>(vsa_ + vbp_ - 1);
    hltdc_.Init.AccumulatedActiveW =
        static_cast<uint32_t>(hsa_ + hbp_ + width_ - 1);
    hltdc_.Init.AccumulatedActiveH =
        static_cast<uint32_t>(vsa_ + vbp_ + height_ - 1);
    hltdc_.Init.TotalWidth =
        static_cast<uint32_t>(hsa_ + hbp_ + width_ + hfp_ - 1);
    hltdc_.Init.TotalHeigh =
        static_cast<uint32_t>(vsa_ + vbp_ + height_ + vfp_ - 1);
    hltdc_.Init.Backcolor.Red = 0;
    hltdc_.Init.Backcolor.Green = 0;
    hltdc_.Init.Backcolor.Blue = 0;
    if (HAL_LTDC_Init(&hltdc_) != HAL_OK) return false;

    LTDC_LayerCfgTypeDef layerCfg = {};
    layerCfg.WindowX0 = 0;
    layerCfg.WindowX1 = static_cast<uint32_t>(width_);
    layerCfg.WindowY0 = 0;
    layerCfg.WindowY1 = static_cast<uint32_t>(height_);
    layerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layerCfg.Alpha = 255;
    layerCfg.Alpha0 = 0;
    layerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    layerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    layerCfg.FBStartAdress = reinterpret_cast<uint32_t>(framebuffer_);
    layerCfg.ImageWidth = static_cast<uint32_t>(width_);
    layerCfg.ImageHeight = static_cast<uint32_t>(height_);
    layerCfg.Backcolor.Red = 0;
    layerCfg.Backcolor.Green = 0;
    layerCfg.Backcolor.Blue = 0;
    if (HAL_LTDC_ConfigLayer(&hltdc_, &layerCfg, 0) != HAL_OK) return false;

    return HAL_DSI_Refresh(&hdsi_) == HAL_OK;
  }

  bool drawBitmap(size_t x0, size_t y0, size_t x1, size_t y1,
                  const uint8_t* rgb565) override {
    // Adapted-command-mode panel: the framebuffer IS the LTDC layer
    // buffer, so a "draw" is a plain memory copy into it, followed by a
    // refresh request telling the DSI wrapper to push the frame out.
    const size_t rowBytes = (x1 - x0) * sizeof(uint16_t);
    const uint8_t* src = rgb565;
    uint8_t* dstBase = reinterpret_cast<uint8_t*>(framebuffer_);
    for (size_t y = y0; y < y1; ++y) {
      uint8_t* dst = dstBase + (y * width_ + x0) * sizeof(uint16_t);
      memcpy(dst, src, rowBytes);
      src += rowBytes;
    }
    return HAL_DSI_Refresh(&hdsi_) == HAL_OK;
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
  uint32_t dpiClockKHz_;
  DSI_PLLInitTypeDef pllInit_;
  uint16_t* framebuffer_;
  uint16_t hsa_ = 10, hbp_ = 10, hfp_ = 10;
  uint16_t vsa_ = 2, vbp_ = 10, vfp_ = 10;
  DSI_HandleTypeDef hdsi_ = {};
  LTDC_HandleTypeDef hltdc_ = {};
  uint8_t* scratch_ = nullptr;
  size_t scratchCapacity_ = 0;
};

#endif  // TINYGPU_HAS_STM32_DSI

}  // namespace tinygpu
