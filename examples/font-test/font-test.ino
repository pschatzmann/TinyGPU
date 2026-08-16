/**
 * @file font-test.ino
 * @brief Hardware test sketch cycling through every TinyGPU font, each
 * rendered at three sizes (scale 1/2/3), to check legibility on a real
 * panel.
 *
 * TinyGPU doesn't ship separate font files per point size - each font is
 * a single fixed glyph bitmap (BitmapFont's built-in 5x7, or one of the
 * AsciiFonts.h fixed sizes: Font8x8, Font8x12, Font12x12, Font16x24) that
 * IFont::drawText()'s `scale` parameter enlarges by an integer factor.
 * This sketch shows both axes: which fonts are available, and what each
 * one looks like scaled up.
 *
 * Tap the screen to cycle to the next font. Serial also prints which
 * font is showing.
 *
 * Tested on a 3.5" 240x320 ILI9341 SPI TFT touch board (the "ESP32 LVGL
 * WiFi & Bluetooth" boards) with a CST816S capacitive touch controller -
 * adjust the pins below (and the touch driver, if yours is different or
 * absent) for your own hardware.
 */
#include <TinyGPU.h>
#include <TinyGPU/DisplayDriverSPI.h>
#include <TinyGPU/SpriteDisplay.h>

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

// --- Touch I2C pins (optional - only used to tap-cycle fonts) ------------
constexpr int8_t kPinTouchSda = 33;
constexpr int8_t kPinTouchScl = 32;
constexpr int8_t kPinTouchIrq = 36;

ILI9341Driver<PixelT> tftDriver(SPI, kPinCs, kPinDc, kPinRst);
SpriteDisplay<PixelT> display(kDisplayWidth, kDisplayHeight, tftDriver,
                              PixelT::fromRGB(0, 0, 0));
TouchDriverCST816S touchDriver(Wire, /*rstPin=*/-1, kPinTouchIrq);

// Fixed 5x7 font used for page headers, independent of whichever font is
// being demonstrated below it on that page.
BitmapFont<PixelT> headerFont;

// One instance of every font TinyGPU ships.
BitmapFont<PixelT> font5x7;
Font8x8<PixelT> font8x8;
Font8x12<PixelT> font8x12;
Font12x12<PixelT> font12x12;
Font16x24<PixelT> font16x24;

struct FontEntry {
  IFont<PixelT>* font;
  const char* name;
};

FontEntry fonts[] = {
    {&font5x7, "BitmapFont (5x7)"},
    {&font8x8, "Font8x8"},
    {&font8x12, "Font8x12"},
    {&font12x12, "Font12x12"},
    {&font16x24, "Font16x24"},
};
constexpr size_t kFontCount = sizeof(fonts) / sizeof(fonts[0]);
size_t fontIndex = 0;

// Single sprite reused for every line on every page, repositioned via
// moveSprite() instead of allocating a new sprite per line. SpriteInfo
// only stores a pointer to whatever surface it's given (see
// SpriteInfo::sprite in SpriteInfo.h), so moveSprite() re-reads
// `line`'s current buffer each time - no per-line allocation needed once
// `line` itself is sized. Fixed at the tallest content any page ever
// needs (Font16x24 @ scale 3); resize() below only ever shrinks/regrows
// within that already-reserved capacity, so it never reallocates either.
constexpr size_t kMaxLineHeight = 24 * 3;
Sprite<PixelT> line(kDisplayWidth, kMaxLineHeight, headerFont);
SpriteInfo<PixelT, Surface<PixelT>>* lineInfo = nullptr;

void drawLine(size_t y, size_t height, IFont<PixelT>& font, const char* text,
             PixelT fg, PixelT bg, uint8_t scale) {
  line.resize(kDisplayWidth, height);
  line.clear(bg);
  line.setFont(font);
  line.drawText(8, 0, text, fg, bg, true, scale);
  if (lineInfo == nullptr) {
    lineInfo = &display.addSprite(0, y, line);
  } else {
    display.moveSprite(*lineInfo, 0, y);
  }
}

// Sample text lengths are chosen so even the widest font (Font16x24, 16px
// glyphs) fits inside the 240px-wide screen at each scale: 11 chars @1x
// (176px), 6 chars @2x (192px), 4 chars @3x (192px), all within the
// ~224px available after margins.
void showFontPage(size_t index) {
  IFont<PixelT>& testFont = *fonts[index].font;
  const PixelT white = PixelT::fromRGB(255, 255, 255);
  const PixelT black = PixelT::fromRGB(0, 0, 0);

  display.clear();
  lineInfo = nullptr;  // display.clear() just dropped the previous SpriteInfo

  char header[48];
  snprintf(header, sizeof(header), "%s (%u/%u)", fonts[index].name,
           static_cast<unsigned>(index + 1), static_cast<unsigned>(kFontCount));
  drawLine(8, 12, headerFont, header, white, black, 1);
  drawLine(20, 12, headerFont, "tap to cycle fonts", white, black, 1);

  size_t y = 40;
  drawLine(y, testFont.getHeight(1), testFont, "AaBbCc 0123", white, black, 1);
  y += testFont.getHeight(1) + 10;
  drawLine(y, testFont.getHeight(2), testFont, "AaBbCc", white, black, 2);
  y += testFont.getHeight(2) + 10;
  drawLine(y, testFont.getHeight(3), testFont, "AaBb", white, black, 3);

  Serial.printf("Font %u/%u: %s\n", static_cast<unsigned>(index + 1),
               static_cast<unsigned>(kFontCount), fonts[index].name);
}

void setup() {
  Serial.begin(115200);

  pinMode(kPinBacklight, OUTPUT);
  digitalWrite(kPinBacklight, HIGH);

  SPI.begin(kPinSclk, kPinMiso, kPinMosi, kPinCs);
  display.begin();
  // The single reused `line` sprite is moved across several positions
  // per page (see drawLine/moveSprite above); the default
  // clear-on-move would erase each already-drawn line as soon as the
  // sprite moves on to draw the next one.
  display.setClearOnSpriteMove(false);

  Wire.begin(kPinTouchSda, kPinTouchScl);
  touchDriver.begin();
  display.setTouchDriver(touchDriver);

  line.begin();
  showFontPage(fontIndex);
}

void loop() {
  static bool wasTouched = false;
  const bool touched = touchDriver.isTouched();
  if (touched && !wasTouched) {
    fontIndex = (fontIndex + 1) % kFontCount;
    showFontPage(fontIndex);
  }
  wasTouched = touched;
  delay(30);
}
