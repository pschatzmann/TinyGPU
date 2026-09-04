#pragma once
#include <stdint.h>

#include "TinyGPU/Abstractions/IQSPIBus.h"
#include "TinyGPU/Emulation.h"

namespace tinygpu {

/**
 * @brief Portable, software-driven IQSPIBus backend built only on
 * pinMode()/digitalWrite() - works on any Arduino core, notably RP2040 and
 * STM32, neither of which exposes a hardware peripheral for this panel
 * family's quad-SPI protocol the way ESP-IDF's esp_lcd does for ESP32
 * (see QSPIBusESP32.h):
 *   - RP2040 has no dedicated quad-SPI-out peripheral; its PIO blocks
 *     could bit-bang this protocol at much higher throughput with a
 *     custom PIO program, but that's a real follow-up project, not
 *     something to get subtly wrong un-tested against real hardware.
 *   - STM32's QUADSPI/OCTOSPI peripheral (where present - H7/F7/L4+/...,
 *     not every part) is designed for external flash/PSRAM (address-phase
 *     memory-mapped or indirect reads/writes), not for driving a display
 *     controller's command/pixel-write protocol, and its pin/AF/clock
 *     setup is per-variant CubeMX territory - not something this library
 *     can wire up generically without a specific target board.
 *
 * Reproduces the same wire protocol QSPIBusESP32/esp_lcd's quad_mode
 * produces (see IQSPIBus.h and DisplayDriverQSPI.h's class doc comment):
 * the 32-bit {opcode, cmd, 0x00, 0x00} frame is always clocked out over
 * all 4 data lines (a nibble per clock, MSB-first); what follows is
 * clocked out over D0 only (1 bit/clock) for a 0x02 ("param") frame, or
 * over all 4 lines (a nibble/clock) for a 0x32 ("color") frame.
 *
 * This is correctness-first, not throughput-first: each bit is one
 * digitalWrite() pair (data setup + clock pulse), so a full-screen
 * writeColor() call is far slower here than on QSPIBusESP32's DMA path.
 * Good enough to bring a panel up and validate the higher layers
 * (NV3041ADriver, ...) on RP2040/STM32; treat clock-limited frame rate as
 * an expected tradeoff of this backend, not a bug.
 */
class QSPIBusBitBang : public IQSPIBus {
 public:
  QSPIBusBitBang(int8_t cs, int8_t sclk, int8_t d0, int8_t d1, int8_t d2,
                int8_t d3)
      : cs_(cs), sclk_(sclk), d0_(d0), d1_(d1), d2_(d2), d3_(d3) {}

  bool begin() override {
    pinMode(cs_, OUTPUT);
    pinMode(sclk_, OUTPUT);
    pinMode(d0_, OUTPUT);
    pinMode(d1_, OUTPUT);
    pinMode(d2_, OUTPUT);
    pinMode(d3_, OUTPUT);
    digitalWrite(cs_, HIGH);
    digitalWrite(sclk_, LOW);  // SPI mode 0: clock idles low
    return true;
  }

  void end() override {}

  bool writeCommand(uint8_t cmd, const uint8_t* param, size_t len) override {
    digitalWrite(cs_, LOW);
    sendFrameQuad(kOpcodeParam, cmd);
    for (size_t i = 0; i < len; ++i) sendByteSingle(param[i]);
    digitalWrite(cs_, HIGH);
    return true;
  }

  bool writeColor(uint8_t cmd, const uint8_t* data, size_t len) override {
    digitalWrite(cs_, LOW);
    sendFrameQuad(kOpcodeColor, cmd);
    for (size_t i = 0; i < len; ++i) sendByteQuad(data[i]);
    digitalWrite(cs_, HIGH);
    return true;
  }

 private:
  static constexpr uint8_t kOpcodeParam = 0x02;
  static constexpr uint8_t kOpcodeColor = 0x32;

  // Clocks one rising edge with the data lines already set - SPI mode 0
  // (CPOL=0, CPHA=0): data is set up while the clock is low, then sampled
  // by the panel on the rising edge.
  void clockPulse() {
    digitalWrite(sclk_, HIGH);
    digitalWrite(sclk_, LOW);
  }

  void sendNibble(uint8_t nibble) {
    digitalWrite(d0_, (nibble >> 0) & 1);
    digitalWrite(d1_, (nibble >> 1) & 1);
    digitalWrite(d2_, (nibble >> 2) & 1);
    digitalWrite(d3_, (nibble >> 3) & 1);
    clockPulse();
  }

  void sendByteQuad(uint8_t b) {
    sendNibble((b >> 4) & 0x0F);
    sendNibble(b & 0x0F);
  }

  void sendByteSingle(uint8_t b) {
    for (int8_t bit = 7; bit >= 0; --bit) {
      digitalWrite(d0_, (b >> bit) & 1);
      clockPulse();
    }
  }

  // The 32-bit {opcode, cmd, 0x00, 0x00} command frame, always sent quad
  // (8 nibbles), matching esp_lcd's quad_mode=1 framing (see IQSPIBus.h).
  void sendFrameQuad(uint8_t opcode, uint8_t cmd) {
    sendByteQuad(opcode);
    sendByteQuad(cmd);
    sendByteQuad(0x00);
    sendByteQuad(0x00);
  }

  int8_t cs_, sclk_, d0_, d1_, d2_, d3_;
};

}  // namespace tinygpu
