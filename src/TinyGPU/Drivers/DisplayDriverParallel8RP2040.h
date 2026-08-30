#pragma once
#include <initializer_list>
#include <stdint.h>
#include <string.h>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Emulation.h"

#if defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040)
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#define TINYGPU_HAS_RP2040_PIO_PARALLEL8 1
#else
#error \
    "DisplayDriverParallel8RP2040.h needs the RP2040 PIO/DMA peripherals (pico-sdk hardware/pio.h, hardware/dma.h) - RP2040-only (arduino-pico core, or a plain pico-sdk build). For a portable, any-Arduino-core bit-banged 8-bit parallel driver see DisplayDriverParallel8.h; for ESP32's hardware LCD_CAM/PARLIO peripherals see DisplayDriverParallel8ESP32.h."
#endif

namespace tinygpu {

#ifdef TINYGPU_HAS_RP2040_PIO_PARALLEL8

// clang-format off
//
// PIO program (assembled with pico-sdk's pioasm from the source below -
// this is its verbatim `-o hex` output, not hand-derived - see the
// comment above each instruction for what it does):
//
//   .program lcd8080_write
//   .side_set 1
//   .wrap_target
//       out pins, 8    side 0   ; drive the byte onto D0-D7, WR low
//       nop            side 1   ; WR high - panel latches on this edge
//   .wrap
//
// Runs at 2 PIO clock cycles/byte. autopull (8-bit threshold, shift
// right) means the state machine pulls a fresh byte from the TX FIFO by
// itself whenever the OSR runs dry - the byte just needs to sit in the
// low 8 bits of whatever 32-bit word reaches the FIFO (from
// pio_sm_put_blocking() or a DMA_SIZE_8 transfer into txf[]).
//
// clang-format on
static const uint16_t kLcd8080WriteProgramInstructions[] = {
    0x6008,  //  0: out    pins, 8         side 0
    0xb042,  //  1: nop                    side 1
};

static const pio_program_t kLcd8080WriteProgram = {
    .instructions = kLcd8080WriteProgramInstructions,
    .length = 2,
    .origin = -1,
};

/**
 * @brief RP2040 PIO/DMA-accelerated 8-bit parallel ("Intel 8080"-style)
 * TFT display driver. D0-D7 and WR are driven by one PIO state machine
 * (see kLcd8080WriteProgram above) instead of bit-banged via
 * digitalWrite() - see DisplayDriverParallel8.h for the portable
 * bit-banged version this mirrors, and DisplayDriverParallel8ESP32.h for
 * the equivalent ESP32 LCD_CAM/PARLIO-peripheral version. DC/CS/RST stay
 * on plain GPIO (Arduino pinMode()/digitalWrite()), same as those two.
 *
 * Bulk pixel data (writeData()) is fed to the state machine via a DMA
 * channel reading directly out of the surface's buffer - the CPU issues
 * one DMA transfer and waits for it (and the state machine's own FIFO) to
 * drain, rather than looping a pio_sm_put_blocking() call per byte.
 *
 * Hardware constraint (PIO's `out pins`/`side-set` addressing): D0-D7
 * must be 8 *consecutive* GPIOs, D0 being the lowest-numbered - this
 * isn't a software choice, it's how the PIO block maps its 32
 * general-purpose output-mapped pins. WR must be a separate GPIO outside
 * that D0-D7 range.
 */
template <typename RGB_T = RGB565>
class DisplayDriverParallel8RP2040 : public DisplayDriver<RGB_T> {
 public:
  /// @param d0 the first (lowest-numbered) of 8 consecutive data-line
  /// GPIOs - see the class comment's hardware constraint.
  /// @param wr write-strobe pin - the PIO program's side-set pin.
  /// @param dc data/command ("RS") select pin.
  /// @param cs chip-select pin, or -1 if the panel has CS permanently
  /// tied low.
  /// @param rst reset pin, or -1 if not wired up.
  /// @param width/height the panel's addressable resolution - subclasses
  /// with a rotation concept (e.g. ILI9341Driver8080RP2040) override
  /// width()/height() themselves instead and can ignore these.
  /// @param clockDiv PIO state machine clock divider - the program runs
  /// at 2 PIO-clock cycles/byte, so the WR toggle rate is
  /// (clock_sys_hz / clockDiv) / 2. Default (8.0) gives roughly 7.8MHz at
  /// a stock 125MHz system clock - comfortably inside most 8080 panels'
  /// timing spec; lower it for a faster panel/wiring, raise it if data
  /// looks corrupted (a symptom of the panel not keeping up).
  /// @param pio which PIO block to use (pio0/pio1) - pick one not
  /// already fully claimed by another peripheral (e.g. an RP2040 audio
  /// driver using PIO for I2S).
  DisplayDriverParallel8RP2040(int8_t d0, int8_t wr, int8_t dc,
                               int8_t cs = -1, int8_t rst = -1,
                               size_t width = 240, size_t height = 320,
                               float clockDiv = 8.0f, PIO pio = pio0)
      : d0_(d0),
        wr_(wr),
        dc_(dc),
        cs_(cs),
        rst_(rst),
        width_(width),
        height_(height),
        clockDiv_(clockDiv),
        pio_(pio) {}

  void end() override {
    if (dma_ >= 0) {
      dma_channel_unclaim(dma_);
      dma_ = -1;
    }
    if (busInitialized_) {
      pio_sm_set_enabled(pio_, sm_, false);
      pio_remove_program(pio_, &kLcd8080WriteProgram, offset_);
      pio_sm_unclaim(pio_, sm_);
      busInitialized_ = false;
    }
  }

  ~DisplayDriverParallel8RP2040() override { end(); }

  size_t width() const override { return width_; }
  size_t height() const override { return height_; }

  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    static_assert(sizeof(RGB_T) == 2,
                  "writeData assumes a 16bpp RGB_T (RGB565) stored in "
                  "wire byte order");
    if (!busInitialized_) return false;
    setAddressWindow(x, y, surface.width(), surface.height());

    beginTransaction();
    digitalWrite(dc_, HIGH);

    dma_channel_config cfg = dma_channel_get_default_config(dma_);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, false);
    channel_config_set_dreq(&cfg, pio_get_dreq(pio_, sm_, true));

    dma_channel_configure(dma_, &cfg, &pio_->txf[sm_], surface.data(),
                          surface.size(), true);
    dma_channel_wait_for_finish_blocking(dma_);
    waitForTxDrain();

    endTransaction();
    return true;
  }

 protected:
  int8_t d0_, wr_, dc_, cs_, rst_;
  size_t width_, height_;
  float clockDiv_;
  PIO pio_;
  uint sm_ = 0;
  uint offset_ = 0;
  int dma_ = -1;
  bool busInitialized_ = false;

  void setColumnRowAddress(size_t x, size_t y, size_t w, size_t h) {
    writeCommand(0x2A);
    writeData16(x, x + w - 1);
    writeCommand(0x2B);
    writeData16(y, y + h - 1);
  }

  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    setColumnRowAddress(x, y, w, h);
    writeCommand(0x2C);  // RAMWR
    return true;
  }

  /// Claims a state machine + DMA channel and starts the PIO program.
  /// Subclasses call this first thing in their begin() (after
  /// setupPinsAndReset()), then send their chip's own init register
  /// sequence via writeCommand()/writeDataN() before returning.
  bool beginBus() {
    int smClaim = pio_claim_unused_sm(pio_, false);
    if (smClaim < 0) return false;
    sm_ = static_cast<uint>(smClaim);

    if (!pio_can_add_program(pio_, &kLcd8080WriteProgram)) return false;
    offset_ = pio_add_program(pio_, &kLcd8080WriteProgram);

    for (int i = 0; i < 8; ++i) {
      pio_gpio_init(pio_, d0_ + i);
    }
    pio_gpio_init(pio_, wr_);
    pio_sm_set_consecutive_pindirs(pio_, sm_, d0_, 8, true);
    pio_sm_set_consecutive_pindirs(pio_, sm_, wr_, 1, true);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset_, offset_ + kLcd8080WriteProgram.length - 1);
    sm_config_set_sideset(&c, 1, false, false);
    sm_config_set_out_pins(&c, d0_, 8);
    sm_config_set_sideset_pins(&c, wr_);
    sm_config_set_out_shift(&c, /*shift_right=*/true, /*autopull=*/true, 8);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    sm_config_set_clkdiv(&c, clockDiv_);

    pio_sm_init(pio_, sm_, offset_, &c);
    pio_sm_set_enabled(pio_, sm_, true);

    const int dmaClaim = dma_claim_unused_channel(false);
    if (dmaClaim < 0) return false;
    dma_ = dmaClaim;

    busInitialized_ = true;
    return true;
  }

  void setupPinsAndReset() {
    if (dc_ >= 0) pinMode(dc_, OUTPUT);
    if (cs_ >= 0) {
      pinMode(cs_, OUTPUT);
      digitalWrite(cs_, HIGH);
    }
    if (rst_ >= 0) {
      pinMode(rst_, OUTPUT);
      digitalWrite(rst_, LOW);
      delay(20);
      digitalWrite(rst_, HIGH);
      delay(150);
    }
  }

  void writeCommand(uint8_t cmd) {
    beginTransaction();
    digitalWrite(dc_, LOW);
    writeByte(cmd);
    waitForTxDrain();
    endTransaction();
  }

  void writeData16(uint16_t d1, uint16_t d2) {
    beginTransaction();
    digitalWrite(dc_, HIGH);
    writeByte(d1 >> 8);
    writeByte(d1 & 0xFF);
    writeByte(d2 >> 8);
    writeByte(d2 & 0xFF);
    waitForTxDrain();
    endTransaction();
  }

  void writeData8(uint8_t data) {
    beginTransaction();
    digitalWrite(dc_, HIGH);
    writeByte(data);
    waitForTxDrain();
    endTransaction();
  }

  /// Writes each byte as a separate writeData8() call - a convenience for
  /// sending a command's multi-byte parameter list during init sequences,
  /// matching DisplayDriverSPI::writeDataN()/DisplayDriverParallel8's
  /// writeDataN().
  void writeDataN(std::initializer_list<uint8_t> bytes) {
    for (uint8_t b : bytes) {
      writeData8(b);
    }
  }

 private:
  void beginTransaction() {
    if (cs_ >= 0) digitalWrite(cs_, LOW);
  }

  void endTransaction() {
    if (cs_ >= 0) digitalWrite(cs_, HIGH);
  }

  /// Pushes one byte into the state machine's TX FIFO - the byte belongs
  /// in the FIFO word's low 8 bits (see the program comment's note on
  /// autopull/shift_right).
  void writeByte(uint8_t data) {
    pio_sm_put_blocking(pio_, sm_, data);
  }

  /// Blocks until the state machine has actually shifted out everything
  /// pushed to it - the TX FIFO alone can report empty slightly before
  /// the OSR finishes shifting the last word out, so this also waits for
  /// the state machine to stall (nothing left to execute against).
  void waitForTxDrain() {
    while (!pio_sm_is_tx_fifo_empty(pio_, sm_)) {
    }
    while (!(pio_->fdebug & (1u << (PIO_FDEBUG_TXSTALL_LSB + sm_)))) {
    }
    pio_->fdebug = 1u << (PIO_FDEBUG_TXSTALL_LSB + sm_);  // write-1-to-clear
  }
};

/**
 * @brief Driver for ILI9341 8-bit parallel display controller, driven via
 * RP2040's PIO/DMA (see DisplayDriverParallel8RP2040).
 *
 * Same power/gamma/MADCTL init sequence as DisplayDriverSPI.h's
 * ILI9341Driver (see that class's doc comment for provenance) - only the
 * bus underneath differs.
 */
template <typename RGB_T = RGB565>
class ILI9341Driver8080RP2040 : public DisplayDriverParallel8RP2040<RGB_T> {
 public:
  using DisplayDriverParallel8RP2040<RGB_T>::setupPinsAndReset;
  using DisplayDriverParallel8RP2040<RGB_T>::beginBus;
  using DisplayDriverParallel8RP2040<RGB_T>::writeCommand;
  using DisplayDriverParallel8RP2040<RGB_T>::writeData8;
  using DisplayDriverParallel8RP2040<RGB_T>::writeDataN;

  using Rotation = tinygpu::DisplayRotation;

  /// @param nativeWidth/nativeHeight the panel's physical resolution in
  /// its native (portrait, MV bit clear) orientation - 240x320 is the
  /// common ILI9341 module size and the default; pass your panel's real
  /// values if it differs.
  ILI9341Driver8080RP2040(int8_t d0, int8_t wr, int8_t dc, int8_t cs = -1,
                          int8_t rst = -1, Rotation rotation = Rotation::kNone,
                          float clockDiv = 8.0f, PIO pio = pio0,
                          size_t nativeWidth = 240, size_t nativeHeight = 320)
      : DisplayDriverParallel8RP2040<RGB_T>(d0, wr, dc, cs, rst, nativeWidth,
                                            nativeHeight, clockDiv, pio),
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
    setupPinsAndReset();
    if (!beginBus()) return false;

    writeCommand(0x01);  // SWRESET
    delay(150);

    writeCommand(0xEF);
    writeDataN({0x03, 0x80, 0x02});

    writeCommand(0xCF);
    writeDataN({0x00, 0xC1, 0x30});

    writeCommand(0xED);
    writeDataN({0x64, 0x03, 0x12, 0x81});

    writeCommand(0xE8);
    writeDataN({0x85, 0x00, 0x78});

    writeCommand(0xCB);
    writeDataN({0x39, 0x2C, 0x00, 0x34, 0x02});

    writeCommand(0xF7);
    writeDataN({0x20});

    writeCommand(0xEA);
    writeDataN({0x00, 0x00});

    writeCommand(0xC0);
    writeDataN({0x23});

    writeCommand(0xC1);
    writeDataN({0x10});

    writeCommand(0xC5);
    writeDataN({0x3E, 0x28});

    writeCommand(0xC7);
    writeDataN({0x86});

    writeCommand(0x36);  // MADCTL
    writeDataN({madctlForRotation(rotation_)});

    writeCommand(0x3A);  // Pixel format: 16bpp
    writeDataN({0x55});

    writeCommand(0xB1);
    writeDataN({0x00, 0x13});

    writeCommand(0xB6);
    writeDataN({0x08, 0x82, 0x27});

    writeCommand(0xF2);
    writeDataN({0x00});

    writeCommand(0x26);
    writeDataN({0x01});

    writeCommand(0xE0);
    writeDataN({0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07,
               0x10, 0x03, 0x0E, 0x09, 0x00});

    writeCommand(0xE1);
    writeDataN({0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08,
               0x0F, 0x0C, 0x31, 0x36, 0x0F});

    writeCommand(0x11);  // Sleep out
    delay(120);
    writeCommand(0x29);  // Display on

    writeCommand(invertColor_ ? 0x21 : 0x20);

    return true;
  }

  void setRotation(Rotation rotation) override {
    if (rotation == Rotation::kNone) return;
    rotation_ = rotation;
    writeCommand(0x36);
    writeDataN({madctlForRotation(rotation_)});
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
 * RP2040's PIO/DMA (see DisplayDriverParallel8RP2040).
 */
template <typename RGB_T = RGB565>
class ST7789Driver8080RP2040 : public DisplayDriverParallel8RP2040<RGB_T> {
 public:
  using DisplayDriverParallel8RP2040<RGB_T>::setupPinsAndReset;
  using DisplayDriverParallel8RP2040<RGB_T>::beginBus;
  using DisplayDriverParallel8RP2040<RGB_T>::writeCommand;
  using DisplayDriverParallel8RP2040<RGB_T>::writeData8;

  ST7789Driver8080RP2040(int8_t d0, int8_t wr, int8_t dc, int8_t cs = -1,
                         int8_t rst = -1, size_t width = 240,
                         size_t height = 320, float clockDiv = 8.0f,
                         PIO pio = pio0)
      : DisplayDriverParallel8RP2040<RGB_T>(d0, wr, dc, cs, rst, width,
                                            height, clockDiv, pio) {}

  bool begin() override {
    setupPinsAndReset();
    if (!beginBus()) return false;
    writeCommand(0x01);  // SWRESET
    delay(150);
    writeCommand(0x11);  // Sleep out
    delay(120);
    writeCommand(0x3A);
    writeData8(0x55);  // Pixel format: 16bpp
    writeCommand(0x29);  // Display on
    return true;
  }
};

#endif  // TINYGPU_HAS_RP2040_PIO_PARALLEL8

}  // namespace tinygpu
