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
#include <Arduino.h>
#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>
#include <TinyGPU/Surface/SpriteDisplay.h>

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

  char logLine[64];
  snprintf(logLine, sizeof(logLine), "Font %u/%u: %s",
          static_cast<unsigned>(index + 1),
          static_cast<unsigned>(kFontCount), fonts[index].name);
  Serial.println(logLine);
}

void setup() {
  Serial.begin(115200);

  display.begin();  // brings up the whole board (see SpriteDisplay(LCDBoard&))
  // The single reused `line` sprite is moved across several positions
  // per page (see drawLine/moveSprite above); the default
  // clear-on-move would erase each already-drawn line as soon as the
  // sprite moves on to draw the next one.
  display.setClearOnSpriteMove(false);

  line.begin();
  showFontPage(fontIndex);
}

void loop() {
  static bool wasTouched = false;
  const bool touched = board.touch()->isTouched();
  if (touched && !wasTouched) {
    fontIndex = (fontIndex + 1) % kFontCount;
    showFontPage(fontIndex);
  }
  wasTouched = touched;
  delay(30);
}
