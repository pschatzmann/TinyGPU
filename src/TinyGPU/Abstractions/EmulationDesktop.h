#pragma once
/**
 * @file EmulationDesktop.h
 * @brief Minimal delay()/millis() for a plain desktop C++ build - no
 * Arduino core, no ESP-IDF.
 *
 * This is the desktop branch of TinyGPU/Emulation.h's fallback - include
 * that instead of this file directly; it decides whether a real Arduino
 * core is available, and routes here only when it isn't and
 * ESP_PLATFORM is also undefined (a plain desktop C++/CMake build that
 * links TinyGPU directly, without the Arduino-Emulator this project's
 * own examples fetch - see the top-level CMakeLists.txt).
 *
 * millis() is what TinyGPU/Input/GestureDetector.h calls internally to
 * time gesture durations (tap/long-press/swipe/drag), so a desktop
 * consumer of GestureDetector.h needs this fallback to even compile;
 * delay() is provided alongside it for that same consumer's own event
 * loop to pace itself with, the same way every example's loop() does.
 *
 * Only delay()/millis() are provided here - unlike EmulationIDF.h's
 * ESP-IDF fallback, there's no generic desktop equivalent of a GPIO pin
 * or an SPI/I2C bus to emulate, so pinMode()/digitalWrite()/SPIClass/
 * TwoWire are intentionally not stubbed out: a file that actually needs
 * one of those (DisplayDriverSPI.h, TouchDriverArduino.h, ...) has no
 * meaningful desktop implementation to fall back to and should fail to
 * compile here rather than silently pretend hardware I/O succeeded.
 */

#include <chrono>
#include <thread>

// Namespaced (not global) like the rest of this library - safe to do
// since every call site (all of TinyGPU/Drivers and TinyGPU/Input) lives
// inside `namespace tinygpu` itself, so unqualified lookup finds these
// the same way it would find them in the global namespace on the
// real-Arduino-core branch of TinyGPU/Emulation.h.
namespace tinygpu {

/// Milliseconds since the epoch, from a monotonic (steady) clock - not
/// "time since boot" the way Arduino's millis() is, but that distinction
/// never matters here: GestureDetector.h only ever subtracts two millis()
/// calls to get an elapsed duration, never reads the absolute value.
inline unsigned long millis() {
  using namespace std::chrono;
  return static_cast<unsigned long>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}

/// Blocks the calling thread for `ms` milliseconds, matching Arduino's
/// delay().
inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

}  // namespace tinygpu
