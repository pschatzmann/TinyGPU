#pragma once
/**
 * @file Emulation.h
 * @brief Drop-in replacement for `#include <Arduino.h>` (plus `<SPI.h>`/
 * `<Wire.h>` where needed) for every file under TinyGPU/Drivers and
 * TinyGPU/Input - include this instead of those directly.
 *
 * Resolves to one of three things, depending on what's available:
 *
 *   - A real Arduino core is on the include path (Arduino IDE/
 *     arduino-cli, an ESP-IDF build with the arduino-esp32 component
 *     present, or the desktop Arduino-Emulator used by this project's
 *     own examples): transparent passthrough to the real
 *     `<Arduino.h>`/`<SPI.h>`/`<Wire.h>` - nothing of this project's own
 *     is defined.
 *   - No real Arduino core, but ESP_PLATFORM is defined (a plain
 *     ESP-IDF component build via `idf_component_register`, without
 *     arduino-esp32): TinyGPU/Emulation/EmulationIDF.h - an ESP-IDF-
 *     native emulation of delay()/millis(), pinMode()/digitalWrite()/
 *     digitalRead(), and SPIClass/TwoWire (+ the global SPI/Wire
 *     instances).
 *   - Neither (a plain desktop C++/CMake build that links TinyGPU
 *     directly, without the Arduino-Emulator this project's own examples
 *     fetch): TinyGPU/Emulation/EmulationDesktop.h - just delay()/
 *     millis(), enough to compile and drive
 *     TinyGPU/Input/GestureDetector.h. See that header for why GPIO/SPI/
 *     I2C aren't stubbed out there.
 *
 * Neither fallback is a general Arduino-core replacement - each only
 * covers the calls this library's own Drivers/Input headers make
 * (verified by grepping them), not the full Arduino API. A sketch built
 * against either fallback that calls other Arduino functions will still
 * need its own additions, or the real Arduino core after all.
 *
 * Both fallbacks define their symbols inside `namespace tinygpu` (not
 * globally) - safe to do since every call site (all of TinyGPU/Drivers
 * and TinyGPU/Input) lives inside that same namespace, so unqualified
 * lookup finds them there. Only the real-Arduino-core branch leaves
 * things where that core put them (global/`arduino::`), since that's not
 * this project's code to move.
 */

#if __has_include(<Arduino.h>)

// Real Arduino core available - use it as-is. <SPI.h>/<Wire.h> are
// separate headers even on real Arduino cores, so pull those in too when
// present (harmless/absent-safe via __has_include - most Drivers/Input
// files only need Arduino.h itself).
#include <Arduino.h>
#if __has_include(<SPI.h>)
#include <SPI.h>
#endif
#if __has_include(<Wire.h>)
#include <Wire.h>
#endif

#elif defined(ESP_PLATFORM)

#include "TinyGPU/Emulation/EmulationIDF.h"

#else

#include "TinyGPU/Emulation/EmulationDesktop.h"

#endif
