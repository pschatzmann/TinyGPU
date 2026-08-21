#pragma once
/**
 * @file LCDBoardsESP32.h
 * @brief One-call setup for ESP32(-S3) boards from the arduino-audio-tools
 * "Audio Boards" wiki that combine an LCD/TFT display with a touch
 * controller (and, on the same board, an I2S audio codec/amp):
 * https://github.com/pschatzmann/arduino-audio-tools/wiki/Audio-Boards
 *
 * Bringing one of these boards up normally means hand-wiring the SPI/QSPI
 * bus, the backlight pin, the display controller's init sequence, the
 * touch controller's I2C bus, and separately tracking the board's I2S
 * pins for the audio side - each board class here does the display/touch
 * part in a single begin() call and exposes the I2S pin assignment so it
 * can be handed to e.g. audio_tools::I2SConfig (TinyGPU itself has no
 * audio-tools dependency, so I2SPins is a plain pin struct, not a
 * driver).
 *
 * Only boards that actually have an LCD are covered - audio-only boards
 * on that wiki page are out of scope for this graphics library. See
 * LCDBoards.h for the platform-independent LCDBoard interface these
 * classes implement.
 */
#include <Arduino.h>

#if defined(ESP32)

#include <SD_MMC.h>
#include <SPI.h>
#include <Wire.h>

#include "DisplayDriverDSI.h"
#include "DisplayDriverQSPI.h"
#include "DisplayDriverSPI.h"
#include "LCDBoards.h"

namespace tinygpu {

/**
 * @brief "2.8" ESP32-S3 Display" - ILI9341 SPI TFT + FT6336G capacitive
 * touch (I2C, shares its bus with the board's ES8311 audio codec).
 * (FBBA0125-002) - also named ESP32-S3 Hosyond Display
 *
 * Display: ILI9341, 240x320, SPI - CS=10 DC=46 SCK=12 MOSI=11 MISO=13
 *          BL=45; RST is tied to the board's EN line, so there is no
 *          dedicated reset pin to drive. Needs display inversion turned
 *          on (done in begin()) or colors render as their photographic
 *          negative - confirmed on real hardware.
 * Touch:   FT6336G (register-compatible with TouchDriverFT6236), I2C -
 *          SDA=16 SCL=15 RST=18, physical IRQ=17. The IRQ pin is not
 *          wired into the driver (left at -1, forcing register polling)
 *          because this line's idle polarity was never confirmed on real
 *          hardware; TouchDriverFT6236::isTouched() would otherwise skip
 *          the I2C read whenever it reads the "wrong" level.
 * I2S (ES8311 codec): MCLK=4 BCK=5 WS=7 DOUT=8 DIN=6. Speaker amp
 *          (FM8002E) enable: IO1, active low.
 * LED: single WS2812-compatible RGB LED on IO42.
 *
 * @note Audio setup: this board's ES8311 codec needs its own I2C init (volume,
 * mic gain, ...), not just I2S pins, so plain I2SStream
 * leaves the codec unconfigured. If the sketch also depends on
 * arduino-audio-driver, use its ready-made board definition instead,
 * which owns the codec init and I2S pins together:
 *
 *   #include "AudioTools.h"
 *   #include "AudioTools/AudioLibs/AudioBoardStream.h"
 *   #include "AudioBoards/ESP32S3HosyondDisplay.h"  // arduino-audio-driver
 *
 *   audio_tools::AudioBoardStream out(audio_driver::ESP32S3HosyondDisplay);
 *   auto cfg = out.defaultConfig(TX_MODE);
 *   out.begin(cfg);
 *   out.setVolume(0.5f);
 *
 * Without arduino-audio-driver, i2s()/setI2SPins() still give the raw
 * MCLK/BCK/WS/DOUT/DIN pins for a plain I2SStream, but the caller is then
 * responsible for the ES8311's I2C init and driving the PA-enable pin
 * (i2s().paEnable, active low per paEnableActiveLow) itself.
 */
class LCDBoardESP32S3_2_8Display : public LCDBoard {
 public:
  /// Sets up the backlight, SPI bus, display controller (incl. inversion),
  /// and touch controller. Returns false if the display or touch begin()
  /// fails.
  bool begin() override {
    pinMode(kPinBacklight, OUTPUT);
    digitalWrite(kPinBacklight, HIGH);

    SPI.begin(kPinSck, kPinMiso, kPinMosi, kPinCs);
    if (!display_.begin()) return false;
    display_.setInvertColor(true);

    Wire.begin(kPinTouchSda, kPinTouchScl);
    return touch_.begin();
  }

  /// Panel width in pixels.
  size_t width() const override { return 240; }
  /// Panel height in pixels.
  size_t height() const override { return 320; }

  /// The board's display driver.
  ILI9341Driver<RGB565>& display() override { return display_; }
  /// The board's touch controller.
  TouchDriver* touch() override { return &touch_; }
  /// The board's I2S/PA pin assignment.
  const I2SPins& i2s() const override { return i2s_; }
  /// The board's LED pin assignment.
  const LEDPins& led() const override { return led_; }

 private:
  static constexpr int8_t kPinCs = 10;
  static constexpr int8_t kPinDc = 46;
  static constexpr int8_t kPinSck = 12;
  static constexpr int8_t kPinMosi = 11;
  static constexpr int8_t kPinMiso = 13;
  static constexpr int8_t kPinBacklight = 45;
  static constexpr int8_t kPinTouchSda = 16;
  static constexpr int8_t kPinTouchScl = 15;
  static constexpr int8_t kPinTouchRst = 18;

  // 80MHz (this panel's SPI clock ceiling on this board's wiring, verified
  // on real hardware) instead of the constructor's 40MHz default - halves
  // per-frame SPI transfer time versus leaving it at the default. Rotation
  // stays kNone here since callers set it explicitly (e.g. via
  // OutputTinyGPU::setRotation()) after begin().
  ILI9341Driver<RGB565> display_{SPI,
                                 kPinCs,
                                 kPinDc,
                                 /*rst=*/-1,
                                 ILI9341Driver<RGB565>::Rotation::kNone,
                                 /*frequencyHz=*/80000000};
  TouchDriverFT6236 touch_{Wire, kPinTouchRst, /*irq=*/-1};
  I2SPins i2s_{/*mclk=*/4,     /*bck=*/5,
               /*ws=*/7,
               /*dataOut=*/8,  /*dataIn=*/6,
               /*paEnable=*/1, /*paEnableActiveLow=*/true};
  LEDPins led_{/*single=*/42};
};

using FBBA0125_002 = LCDBoardESP32S3_2_8Display;
using ESP32S3HosyondDisplay = LCDBoardESP32S3_2_8Display;

/**
 * @brief "Guition ESP32-S3 4.3" 480x272 Capacitive Touch Display" (JC4827W543C_I): it has a NV3041A QSPI display, GT911 capacitive touch
 * NV3041A QSPI TFT + GT911 capacitive touch (I2C) + NS4168 speaker amp
 * (speaker output only - no codec/mic on this board).
 * 
 *
 * Display: NV3041A, 480x272, QSPI - CS=45 SCLK=47 D0=21 D1=48 D2=40 D3=39
 *          BL=1, 32MHz.
 * Touch:   GT911, I2C - SDA=8 SCL=4 INT=3 RST=38, address 0x5D.
 * I2S (NS4168 amp): BCK=42 WS=2 DATA=41.
 * LED: none on this board.
 *
 * @note Audio setup: no codec on this board - the NS4168 is a plain I2S power
 * amp, so audio_tools::I2SStream (TX only; there's no mic/data_in) is
 * enough:
 *
 *   #include "AudioTools.h"
 *
 *   I2SStream out;
 *   auto cfg = out.defaultConfig(TX_MODE);
 *   cfg.sample_rate = 44100; cfg.channels = 2; cfg.bits_per_sample = 16;
 *   board.setI2SPins<I2SConfig>(cfg);
 *   out.begin(cfg);
 */
class LCDBoardGuitionESP32S3_4_3Display : public LCDBoard {
 public:
  /// Sets up the backlight, QSPI display controller, and touch controller.
  /// Returns false if the display or touch begin() fails.
  bool begin() override {
    pinMode(kPinBacklight, OUTPUT);
    digitalWrite(kPinBacklight, HIGH);

    if (!display_.begin()) return false;

    Wire.begin(kPinTouchSda, kPinTouchScl);
    return touch_.begin();
  }

  /// Panel width in pixels.
  size_t width() const override { return 480; }
  /// Panel height in pixels.
  size_t height() const override { return 272; }

  /// The board's display driver.
  NV3041ADriver<RGB565>& display() override { return display_; }
  /// The board's touch controller.
  TouchDriver* touch() override { return &touch_; }
  /// The board's I2S pin assignment.
  const I2SPins& i2s() const override { return i2s_; }
  /// This board has no RGB LED - every LEDPins field is -1.
  const LEDPins& led() const override { return led_; }

 private:
  static constexpr int8_t kPinCs = 45;
  static constexpr int8_t kPinSclk = 47;
  static constexpr int8_t kPinD0 = 21;
  static constexpr int8_t kPinD1 = 48;
  static constexpr int8_t kPinD2 = 40;
  static constexpr int8_t kPinD3 = 39;
  static constexpr int8_t kPinBacklight = 1;
  static constexpr int8_t kPinTouchSda = 8;
  static constexpr int8_t kPinTouchScl = 4;
  static constexpr int8_t kPinTouchIrq = 3;
  static constexpr int8_t kPinTouchRst = 38;

  NV3041ADriver<RGB565> display_{kPinCs, kPinSclk, kPinD0,
                                 kPinD1, kPinD2,   kPinD3};
  TouchDriverGT911 touch_{Wire, kPinTouchRst, kPinTouchIrq};
  I2SPins i2s_{/*mclk=*/-1, /*bck=*/42, /*ws=*/2, /*dataOut=*/41,
               /*dataIn=*/-1};
  LEDPins led_{};
};

using JC4827W543C_I = LCDBoardGuitionESP32S3_4_3Display;

/**
 * @brief "ESP32 Arduino LVGL WiFi&Bluetooth Development Board, 2.4" LCD
 * TFT Module" - ILI9341 SPI TFT + CST816S capacitive touch, no PSRAM.
 * (ESP32-2432S028R) - also named ESP32 Cheap Yellow Diplay
 * 
 * 
 * This is the same board pinout TinyGPU's own examples (e.g.
 * examples/color-test) are tested against.
 *
 * Display: ILI9341, 240x320, SPI - CS=15 DC=2 SCK=14 MOSI=13 MISO=12
 *          RST=-1 BL=27.
 * Touch:   CST816S, I2C - SDA=33 SCL=32 IRQ=36 (no RST pin wired).
 * Audio: built-in mono amplifier driven from the ESP32's internal DAC
 *        (analog output, not I2S) - i2s() is all -1 on this board.
 * LED: discrete RGB LED (3 separate GPIOs, active low) - R=4 G=16 B=17.
 *
 * @note Audio setup: this board has no I2S bus at all (i2s().isValid() is
 * false) - use audio_tools::AnalogAudioStream instead of I2SStream. It
 * drives the ESP32's built-in DAC pins (GPIO25/26) directly, so there
 * are no board pins to pass in:
 *
 *   #include "AudioTools.h"
 *
 *   AnalogAudioStream out;
 *   auto cfg = out.defaultConfig(TX_MODE);
 *   cfg.sample_rate = 44100; cfg.channels = 1; cfg.bits_per_sample = 16;
 *   out.begin(cfg);
 */
class LCDBoardGuitionESP32_LVGL_2_4Display : public LCDBoard {
 public:
  /// Sets up the backlight, SPI bus, display controller, and touch
  /// controller. Returns false if the display or touch begin() fails.
  bool begin() override {
    pinMode(kPinBacklight, OUTPUT);
    digitalWrite(kPinBacklight, HIGH);

    SPI.begin(kPinSck, kPinMiso, kPinMosi, kPinCs);
    if (!display_.begin()) return false;

    Wire.begin(kPinTouchSda, kPinTouchScl);
    return touch_.begin();
  }

  /// Panel width in pixels.
  size_t width() const override { return 240; }
  /// Panel height in pixels.
  size_t height() const override { return 320; }

  /// The board's display driver.
  ILI9341Driver<RGB565>& display() override { return display_; }
  /// The board's touch controller.
  TouchDriver* touch() override { return &touch_; }
  /// Analog-DAC audio on this board - every I2SPins field is -1.
  const I2SPins& i2s() const override { return i2s_; }
  /// The board's LED pin assignment.
  const LEDPins& led() const override { return led_; }

 private:
  static constexpr int8_t kPinCs = 15;
  static constexpr int8_t kPinDc = 2;
  static constexpr int8_t kPinSck = 14;
  static constexpr int8_t kPinMosi = 13;
  static constexpr int8_t kPinMiso = 12;
  static constexpr int8_t kPinBacklight = 27;
  static constexpr int8_t kPinTouchSda = 33;
  static constexpr int8_t kPinTouchScl = 32;
  static constexpr int8_t kPinTouchIrq = 36;

  ILI9341Driver<RGB565> display_{SPI, kPinCs, kPinDc, /*rst=*/-1};
  TouchDriverCST816S touch_{Wire, /*rst=*/-1, kPinTouchIrq};
  I2SPins i2s_{};
  LEDPins led_{/*single=*/-1, /*r=*/4, /*g=*/16, /*b=*/17,
               /*activeLow=*/true};
};

using ESP32_2432S028R = LCDBoardGuitionESP32_LVGL_2_4Display;
using ESP32CheapYellowDisplay = LCDBoardGuitionESP32_LVGL_2_4Display;

/**
 * @brief "Guition ESP32-H4 4.3" 480x800 Capacitive Touch Display -
 * (JC4880P443C_I_W): it has a ST7701 MIPI-DSI display, GT911 capacitive touch
 (I2C), and an ES8311 audio codec with NS4150 speaker amp (speaker output only -
 this board has no mic). The board also has a WiFi SDIO interface and an SD_MMC
 interface, both of which need their own pin setup.
 *
 *  * @note Audio setup: this board's ES8311 codec needs its own I2C init
 (volume,
 * mic gain, ...), not just I2S pins, so plain I2SStream
 * leaves the codec unconfigured. If the sketch also depends on
 * arduino-audio-driver, use its ready-made board definition instead,
 * which owns the codec init and I2S pins together:
 *
 *   #include "AudioTools.h"
 *   #include "AudioTools/AudioLibs/AudioBoardStream.h"
 *   #include "AudioBoards/ESP32S3HosyondDisplay.h"  // arduino-audio-driver
 *
 *   audio_tools::AudioBoardStream out(audio_driver::ESP32S3HosyondDisplay);
 *   auto cfg = out.defaultConfig(TX_MODE);
 *   out.begin(cfg);
 *   out.setVolume(0.5f);
 *
 * Without arduino-audio-driver, i2s()/setI2SPins() still give the raw
 * MCLK/BCK/WS/DOUT/DIN pins for a plain I2SStream, but the caller is then
 * responsible for the ES8311's I2C init and driving the PA-enable pin
 * (i2s().paEnable, active low per paEnableActiveLow) itself.

 */
class LCDBoardGuitionESP32H4_4_3Display : public LCDBoard {
 public:
  /// Sets up the backlight, QSPI display controller, and touch controller.
  /// Returns false if the display or touch begin() fails.
  bool begin() override {
    pinMode(kPinLcdBacklight, OUTPUT);
    digitalWrite(kPinLcdBacklight, HIGH);

    if (!display_.begin()) return false;

    Wire.begin(kPinTouchSda, kPinTouchScl);
    if (!touch_.begin()) return false;

    // set wifi pins
    if (!setupWifi()) return false;

    // set SD_MMC pins
    if (!SD_MMC.setPins(kPinSdClk, kPinSdCmd, kPinSdD0, kPinSdD1, kPinSdD2,
                        kPinSdD3)) {
      return false;
    }

    return true;
  }

  /// Panel width in pixels.
  size_t width() const override { return kDisplayWidth; }
  /// Panel height in pixels.
  size_t height() const override { return kDisplayHeight; }

  /// The board's display driver.
  ST7701Driver<RGB565>& display() override { return display_; }
  /// The board's touch controller.
  TouchDriver* touch() override { return &touch_; }
  /// The board's I2S pin assignment.
  const I2SPins& i2s() const override { return i2s_; }
  /// This board has no RGB LED - every LEDPins field is -1.
  const LEDPins& led() const override { return led_; }

 private:
  static constexpr int kPinI2sMclk = 13;
  static constexpr int kPinI2sBck = 12;
  static constexpr int kPinI2sWs = 10;
  static constexpr int kPinI2sDout = 9;  // P4 -> codec
  static constexpr int kPinI2sDin = 48;
  static constexpr int kPinCodecScl = 8;  // shared with GT911 touch bus
  static constexpr int kPinCodecSda = 7;
  static constexpr int kPinAmpEnable = 11;  // NS4150 PA_EN

  // sdcard pins (SDMMC1, 4-bit mode) -
  static constexpr int kPinSdClk = 43;
  static constexpr int kPinSdCmd = 44;
  static constexpr int kPinSdD0 = 39;
  static constexpr int kPinSdD1 = 40;
  static constexpr int kPinSdD2 = 41;
  static constexpr int kPinSdD3 = 42;
  static constexpr int kSdLdoChannel = 4;  // TF_VCC - see file header

  // --- display geometry (native portrait) --------------------------------
  static constexpr int kDisplayWidth = 480;
  static constexpr int kDisplayHeight = 800;

  // --- Display pins/config (ST7701S MIPI-DSI), from
  // guition-jc4880p4-bsp's board_p4_pins.h/board_p4.c ---------------------
  static constexpr int8_t kPinLcdRst = 5;
  static constexpr int8_t kPinLcdBacklight = 23;
  static constexpr int8_t kDsiPhyLdoChan = 3;
  static constexpr uint16_t kDsiPhyLdoMv = 2500;

  // --- Touch I2C pins (GT911), same source ---------------------------------
  static constexpr int8_t kPinTouchSda = 7;
  static constexpr int8_t kPinTouchScl = 8;
  static constexpr int8_t kPinTouchRst = 3;
  static constexpr int8_t kPinTouchInt = -1;  // unused/polled on this board

  // --- WiFi SDIO pins (SDIO1), same source ---------------------------------
  static constexpr int kPinWifiSdioClk = 18;
  static constexpr int kPinWifiSdioCmd = 19;
  static constexpr int kPinWifiSdioD0 = 14;
  static constexpr int kPinWifiSdioD1 = 15;
  static constexpr int kPinWifiSdioD2 = 16;
  static constexpr int kPinWifiSdioD3 = 17;
  static constexpr int kPinWifiC6Reset = 54;

  ST7701Driver<RGB565> display_{kPinLcdRst,     kPinLcdBacklight, kDisplayWidth,
                                kDisplayHeight, kDsiPhyLdoChan,   kDsiPhyLdoMv};
  TouchDriverGT911 touch_{Wire, kPinTouchRst, kPinTouchInt};

  I2SPins i2s_{kPinI2sMclk, kPinI2sBck, kPinI2sWs, kPinI2sDout, kPinI2sDin};
  LEDPins led_{};

  bool setupWifi() {
    return hostedSetPins(kPinWifiSdioClk, kPinWifiSdioCmd, kPinWifiSdioD0,
                         kPinWifiSdioD1, kPinWifiSdioD2, kPinWifiSdioD3,
                         kPinWifiC6Reset);
  }
};

using JC4880P443C_I_W = LCDBoardGuitionESP32H4_4_3Display;

}  // namespace tinygpu

#endif  // ESP32
