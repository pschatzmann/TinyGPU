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
 * Cross-platform: unchanged source runs on both an ESP32 board and the
 * SDL2 desktop backend (mouse click stands in for a tap - see
 * TouchDriverSDL), since both are reached through the same LCDBoard
 * interface (see LCDBoards.h, which picks the right board class per
 * platform automatically).
 *
 * On ESP32 this targets the ESP32 Cheap Yellow Display (ESP32-2432S028R),
 * a 240x320 ILI9341 SPI TFT board with a CST816S capacitive touch
 * controller - built up via the LCDBoardGuitionESP32_LVGL_2_4Display board
 * class (LCDBoardsESP32.h), so its pin wiring doesn't need to be repeated
 * here. Swap the board type below for a different LCDBoard if yours
 * differs. On desktop it opens an SDL2 window of the same size via
 * LCDBoardDesktopSDL.
 */
#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>
#include <TinyGPU/Surface/SpriteDisplay.h>

// Plain RGB565 - no field-swap compensation needed. The earlier color
// rotation/tint on this panel wasn't a field-wiring quirk at all; it was
// DisplayDriverSPI sending each 16-bit pixel's bytes in the wrong order
// (see the writePixels() comment in DisplayDriverSPI.h).
using PixelT = RGB565;

// --- display geometry ---------------------------------------------------
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 320;

#ifdef ESP32
LCDBoardGuitionESP32_LVGL_2_4Display board;
#else
LCDBoardDesktopSDL board(kDisplayWidth, kDisplayHeight);
#endif
SpriteDisplay<PixelT> display(board);
BitmapFont<PixelT> font;

// Draws a solid band with a text label burned into it. Safe against the
// dangling-pointer trap of SpriteDisplay::addSprite(x, y, surface&) - the
// surface is heap-allocated and ownership is handed to SpriteDisplay via
// isSurfaceAutoDelete, the same pattern SpriteDisplay's own
// addSprite(x, y, maxX, maxY, color) overload uses internally.
void addLabeledBand(size_t x, size_t y, size_t w, size_t h, PixelT bgColor,
                    PixelT textColor, const char* label) {
  auto sprite = std::make_unique<Sprite<PixelT>>(w, h, font);
  sprite->begin();
  sprite->clear(bgColor);
  sprite->drawText(4, static_cast<int16_t>(h / 2 - 4), label, textColor, bgColor,
                   true);
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
  static bool wasTouched = false;
  const bool touched = board.touch()->isTouched();
  if (touched && !wasTouched) {
    toggleTest();
  }
  wasTouched = touched;
  delay(30);
}
