/**
 * Memory optimized example using the SpriteDisplay class to draw a single
 * sprite on the screen. The sprite is drawn directly on the screen without
 * using a framebuffer to minimize the required memory. The Sprites are also
 * supporting touch events and can be moved, scaled and rotated.
 *
 * Tested using a 3.5" 240x320 ILI9341 SPI TFT touch board, e.g. the "ESP32 LVGL
 * WiFi & Bluetooth" development boards sold with a 3.5" 240x320 touch display.
 *
 * This board family is sold in two touch variants that look identical but
 * use completely different touch hardware: resistive (XPT2046 over SPI) and
 * capacitive (CST816S over I2C). Verified against real hardware: this unit
 * is the capacitive variant - probing the XPT2046 SPI pins always read back
 * zero, while scanning I2C found a device at 0x15 (SDA=33, SCL=32), which is
 * the CST816S's fixed address. If your board is the resistive variant
 * instead, swap TouchDriverCST816S below for TouchDriverXPT2046 and use the
 * SPI touch pins (see git history of this file for that wiring).
 *
 * This panel's ILI9341-compatible controller also doesn't honor the MADCTL
 * BGR/RGB bit per the datasheet - colors sent as standard RGB565 come out
 * with green and blue swapped (confirmed on real hardware: a green fill
 * shows up blue and vice versa). This example no longer compensates for
 * that quirk (the RBG565 field-swap workaround was removed); if you hit
 * it on your own panel, swap the green/blue arguments at each color
 * constructor call below as a manual workaround.
 *
 * pinch_in/pinch_out/rotate need a second simultaneous touch point, which
 * this board's single-touch CST816S can never supply, so they won't fire
 * here even though GestureDetector supports them for multi-touch hardware.
 */
#include <TinyGPU.h>
#include <TinyGPU/DisplayDriverSPI.h>
#include <TinyGPU/SpriteDisplay.h>
#include <TinyGPU/GestureDetector.h>
#include <lvgl.h>

// --- display geometry (Landscape) ----------------------------------------
constexpr int kDisplayWidth = 240;   // 320;  // Swapped for Landscape
constexpr int kDisplayHeight = 320;  // 240;

// --- SPI / display pins ---------------------------------------------------
constexpr int8_t kPinMosi = 13;
constexpr int8_t kPinMiso = 12;
constexpr int8_t kPinSclk = 14;
constexpr int8_t kPinCs = 15;
constexpr int8_t kPinDc = 2;
constexpr int8_t kPinRst = -1;
constexpr int8_t kPinBacklight = 27;

// --- Touch I2C Pins (CST816S capacitive touch) -----------------------------
constexpr int8_t kPinTouchSda = 33;
constexpr int8_t kPinTouchScl = 32;
constexpr int8_t kPinTouchIrq = 36;

RGB565 red(255, 0, 0);
RGB565 green(0, 255, 0);
RGB565 blue(0, 0, 255);
RGB565 white(255, 255, 255);
RGB565 black(0, 0, 0);

TouchDriverCST816S touchDriver(Wire, /*rstPin=*/-1, kPinTouchIrq);
ILI9341Driver<RGB565> tftDriver(SPI, kPinCs, kPinDc, kPinRst);
SpriteDisplay<RGB565> display(kDisplayWidth, kDisplayHeight, tftDriver, red);
GestureDetector gestures;

SpriteInfo<RGB565, Surface<RGB565>>* draggedSprite = nullptr;
size_t draggedSpriteStartX = 0;
size_t draggedSpriteStartY = 0;

// GestureDetector calls this to decide whether a move should be reported as
// kDrag (over a sprite) or kPan/kScroll (over the background).
bool isOverSprite(int16_t x, int16_t y) {
  return display.getSprite(x, y) != nullptr;
}

void onGesture(GestureEvent& e) {
  Serial.printf("[Gesture] %s at (%d, %d) delta=(%d, %d) duration=%lums\n",
                toString(e.type), e.point.x, e.point.y, e.deltaX, e.deltaY,
                static_cast<unsigned long>(e.durationMs));

  if (e.type != GestureType::kDrag) return;

  if (e.phase == GesturePhase::kBegan) {
    draggedSprite = display.getSprite(e.startPoint.x, e.startPoint.y);
    if (draggedSprite) {
      draggedSpriteStartX = draggedSprite->x;
      draggedSpriteStartY = draggedSprite->y;
    }
  } else if (e.phase == GesturePhase::kChanged && draggedSprite) {
    int newX = static_cast<int>(draggedSpriteStartX) + e.deltaX;
    int newY = static_cast<int>(draggedSpriteStartY) + e.deltaY;
    if (newX < 0) newX = 0;
    if (newY < 0) newY = 0;
    display.moveSprite(*draggedSprite, static_cast<size_t>(newX), static_cast<size_t>(newY));
  } else if (e.phase == GesturePhase::kEnded) {
    draggedSprite = nullptr;
  }
}

void setup() {
  Serial.begin(115200);

  // activate backlight
  pinMode(kPinBacklight, OUTPUT);
  digitalWrite(kPinBacklight, HIGH);

  // setup display
  SPI.begin(kPinSclk, kPinMiso, kPinMosi, kPinCs);
  display.setTouchDriver(touchDriver);
  display.begin();

  // setup touch
  Wire.begin(kPinTouchSda, kPinTouchScl);
  touchDriver.begin();

  display.addSprite(20, 20, 90, 90, green);
  display.addSprite(130, 20, 90, 90, blue);

  gestures.onGesture = onGesture;
  gestures.isDraggable = isOverSprite;
}

void loop() { gestures.update(touchDriver); }
