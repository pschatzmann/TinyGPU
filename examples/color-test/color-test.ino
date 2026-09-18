/**
 * @file color-test.ino
 * @brief SpriteDisplay-based hardware color diagnostic for TFT panels: a
 * red/green/blue band test and a 20-band greyscale ramp test.
 *
 * Useful for two separate things:
 *  - Verifying a panel's actual RGB field wiring. Some cheap
 *    ILI9341-compatible clone controllers don't honor the MADCTL BGR/RGB
 *    bit per the datasheet and route color data to the wrong physical
 *    subpixels - the RGB test makes that obvious (e.g. the "green" band
 *    showing up blue).
 *  - Spotting low-brightness color tint. Some panels show a visible hue
 *    in near-black/grey tones even with field wiring correct - the
 *    greyscale ramp makes that easy to see across the full brightness
 *    range. See GammaTable.h / LVGLDriver::setGamma() for compensating.
 *
 * Tap the screen to switch between the two tests. Serial also prints a
 * description of each band, in case a tint or swap makes a band
 * ambiguous by eye alone.
 *
 * Cross-platform: unchanged source runs on an ESP32 board, an STM32 board,
 * and the SDL2 desktop backend (mouse click stands in for a tap - see
 * TouchDriverSDL), since all three are reached through the same LCDBoard
 * interface (see LCDBoards.h, which picks the right board class per
 * platform automatically).
 *
 * On ESP32 this targets the ESP32 Cheap Yellow Display (ESP32-2432S028R),
 * a 240x320 ILI9341 SPI TFT board with a CST816S capacitive touch
 * controller - built up via the LCDBoardGuitionESP32_LVGL_2_4Display board
 * class (LCDBoardsESP32.h). On STM32 (build for FQBN
 * STMicroelectronics:stm32:GenH7:pnum=WeActMiniH750VBTX) it targets the
 * WeAct MiniSTM32H7xx core board's bundled 0.96" 160x80 ST7735 TFT, via
 * LCDBoardWeActMiniSTM32H750 (LCDBoardsSTM32.h) - that board has no touch
 * controller, so the RGB/greyscale tests auto-cycle on a timer instead of
 * waiting for a tap (see the touch-vs-timer branch in loop()). Swap the
 * board type below for a different LCDBoard if yours differs. On desktop
 * it opens an SDL2 window of the same size via LCDBoardDesktopSDL.
 */
#include <Arduino.h>
#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>
#include <TinyGPU/Surface/SpriteDisplay.h>

// Plain RGB565 - no field-swap compensation needed. The earlier color
// rotation/tint on this panel wasn't a field-wiring quirk at all; it was
// DisplayDriverSPI sending each 16-bit pixel's bytes in the wrong order
// (see the writePixels() comment in DisplayDriverSPI.h).
using PixelT = RGB565;

// --- display geometry ---------------------------------------------------
#if defined(ESP32)
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 320;
LCDBoardGuitionESP32_LVGL_2_4Display board;
#elif defined(ARDUINO_ARCH_STM32)
constexpr int kDisplayWidth = 160;
constexpr int kDisplayHeight = 80;
LCDBoardWeActMiniSTM32H750 board;
#else
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 320;
LCDBoardDesktopSDL board(kDisplayWidth, kDisplayHeight);
#endif
SpriteDisplay<PixelT> display(board);
Font5x7<PixelT> font;

// Draws a solid band, with a text label burned in only if the band is
// tall enough for the font to actually fit - on small displays (e.g. this
// board's 80px-tall panel, sliced into 20 greyscale bands of just 4px
// each), the label would otherwise render as illegible, overlapping
// noise rather than being silently skipped. Safe against the
// dangling-pointer trap of SpriteDisplay::addSprite(x, y, surface&) - the
// surface is heap-allocated and ownership is handed to SpriteDisplay via
// isSurfaceAutoDelete, the same pattern SpriteDisplay's own
// addSprite(x, y, maxX, maxY, color) overload uses internally.
void addLabeledBand(size_t x, size_t y, size_t w, size_t h, PixelT bgColor,
                    PixelT textColor, const char* label) {
  auto sprite = std::make_unique<Sprite<PixelT>>(w, h, font);
  sprite->begin();
  sprite->clear(bgColor);
  if (h >= Font5x7<PixelT>::kGlyphHeight) {
    sprite->drawText(4, static_cast<int16_t>(h / 2 - 4), label, textColor,
                     bgColor, true);
  }
  auto& info = display.addSprite(x, y, *sprite);
  info.isSurfaceAutoDelete = true;
  sprite.release();
}

void showRgbTest() {
  display.clear();
  const size_t bandHeight = kDisplayHeight / 3;
  const size_t lastBandHeight = kDisplayHeight - 2 * bandHeight;

  addLabeledBand(0, 0 * bandHeight, kDisplayWidth, bandHeight,
                PixelT::fromRGB(255, 0, 0), PixelT::fromRGB(255, 255, 255),
                "RED (255,0,0)");
  addLabeledBand(0, 1 * bandHeight, kDisplayWidth, bandHeight,
                PixelT::fromRGB(0, 255, 0), PixelT::fromRGB(0, 0, 0),
                "GREEN (0,255,0)");
  addLabeledBand(0, 2 * bandHeight, kDisplayWidth, lastBandHeight,
                PixelT::fromRGB(0, 0, 255), PixelT::fromRGB(255, 255, 255),
                "BLUE (0,0,255)");

  Serial.println(
      "RGB test: top=RED(255,0,0) mid=GREEN(0,255,0) bottom=BLUE(0,0,255)");
}

void showGreyscaleTest() {
  display.clear();
  constexpr int kBandCount = 20;
  const size_t bandHeight = kDisplayHeight / kBandCount;

  Serial.println("Greyscale test: 20 bands, top=0 (black) to bottom=255 (white)");
  for (int i = 0; i < kBandCount; ++i) {
    const uint8_t value = static_cast<uint8_t>((255 * i) / (kBandCount - 1));
    const PixelT color = PixelT::fromRGB(value, value, value);
    const PixelT textColor =
        (value > 127) ? PixelT::fromRGB(0, 0, 0) : PixelT::fromRGB(255, 255, 255);
    char label[16];
    snprintf(label, sizeof(label), "%2d: %3d", i, value);
    addLabeledBand(0, i * bandHeight, kDisplayWidth, bandHeight, color,
                  textColor, label);
    char logLine[48];
    snprintf(logLine, sizeof(logLine), "  band %2d: grey %3d  packed 0x%04X",
             i, value, color.getValueSwapped());
    Serial.println(logLine);
  }
}

bool showingGreyscale = false;

void toggleTest() {
  showingGreyscale = !showingGreyscale;
  if (showingGreyscale) {
    showGreyscaleTest();
  } else {
    showRgbTest();
  }
}

void setup() {
  Serial.begin(115200);

  display.begin();  // brings up the whole board (see SpriteDisplay(LCDBoard&))

  showRgbTest();
}

void loop() {
  // board.touch() is nullptr on boards with no touch controller (e.g.
  // LCDBoardWeActMiniSTM32H750) - fall back to auto-cycling the tests on a
  // timer instead of waiting for a tap that can never come.
  TouchDriver* touch = board.touch();
  if (touch != nullptr) {
    static bool wasTouched = false;
    const bool touched = touch->isTouched();
    if (touched && !wasTouched) {
      toggleTest();
    }
    wasTouched = touched;
  } else {
    static uint32_t lastSwitchMs = 0;
    const uint32_t now = millis();
    if (now - lastSwitchMs >= 3000) {
      toggleTest();
      lastSwitchMs = now;
    }
  }
  delay(30);
}
