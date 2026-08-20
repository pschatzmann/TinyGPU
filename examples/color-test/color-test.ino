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
 * Tested on a 3.5" 240x320 ILI9341 SPI TFT touch board (the "ESP32 LVGL
 * WiFi & Bluetooth" boards) with a CST816S capacitive touch controller -
 * adjust the pins below (and the touch driver, if yours is different or
 * absent) for your own hardware.
 */
#include <TinyGPU.h>
#include <TinyGPU/DisplayDriverSPI.h>
#include <TinyGPU/SpriteDisplay.h>

// Plain RGB565 - no field-swap compensation needed. The earlier color
// rotation/tint on this panel wasn't a field-wiring quirk at all; it was
// DisplayDriverSPI sending each 16-bit pixel's bytes in the wrong order
// (see the writePixels() comment in DisplayDriverSPI.h).
using PixelT = RGB565;

// --- display geometry ---------------------------------------------------
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 320;

// --- SPI / display pins ---------------------------------------------------
constexpr int8_t kPinMosi = 13;
constexpr int8_t kPinMiso = 12;
constexpr int8_t kPinSclk = 14;
constexpr int8_t kPinCs = 15;
constexpr int8_t kPinDc = 2;
constexpr int8_t kPinRst = -1;
constexpr int8_t kPinBacklight = 27;

// --- Touch I2C pins (optional - only used to tap-switch tests) -----------
constexpr int8_t kPinTouchSda = 33;
constexpr int8_t kPinTouchScl = 32;
constexpr int8_t kPinTouchIrq = 36;

ILI9341Driver<PixelT> tftDriver(SPI, kPinCs, kPinDc, kPinRst);
SpriteDisplay<PixelT> display(kDisplayWidth, kDisplayHeight, tftDriver,
                              PixelT::fromRGB(0, 0, 0));
TouchDriverCST816S touchDriver(Wire, /*rstPin=*/-1, kPinTouchIrq);
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
    Serial.printf("  band %2d: grey %3d  packed 0x%04X\n", i, value,
                  color.getValueSwapped());
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

  pinMode(kPinBacklight, OUTPUT);
  digitalWrite(kPinBacklight, HIGH);

  SPI.begin(kPinSclk, kPinMiso, kPinMosi, kPinCs);
  display.begin();

  Wire.begin(kPinTouchSda, kPinTouchScl);
  touchDriver.begin();
  display.setTouchDriver(touchDriver);

  showRgbTest();
}

void loop() {
  static bool wasTouched = false;
  const bool touched = touchDriver.isTouched();
  if (touched && !wasTouched) {
    toggleTest();
  }
  wasTouched = touched;
  delay(30);
}
