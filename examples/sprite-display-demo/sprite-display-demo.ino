/**
 * Memory optimized example using the SpriteDisplay class to draw a single
 * sprite on the screen. The sprite is drawn directly on the screen without
 * using a framebuffer to minimize the required memory. The Sprites are also
 * supporting touch events and can be moved, scaled and rotated.
 *
 * Tested on the ESP32 Cheap Yellow Display (ESP32-2432S028R), a 240x320
 * ILI9341 SPI TFT board with a CST816S capacitive touch controller -
 * built up here via the LCDBoardGuitionESP32_LVGL_2_4Display board class
 * (LCDBoardsESP32.h), so its pin wiring doesn't need to be repeated here.
 * Swap the board type below for a different LCDBoard if yours differs -
 * e.g. a resistive-touch (XPT2046) variant of this board family needs a
 * different LCDBoard/TouchDriver pairing than this capacitive one.
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
#include <TinyGPU/Boards/LCDBoardsESP32.h>
#include <TinyGPU/Surface/SpriteDisplay.h>
#include <TinyGPU/Input/GestureDetector.h>
#include <lvgl.h>

RGB565 red(255, 0, 0);
RGB565 green(0, 255, 0);
RGB565 blue(0, 0, 255);
RGB565 white(255, 255, 255);
RGB565 black(0, 0, 0);

ESP32CheapYellowDisplay board;
SpriteDisplay<RGB565> display(board, red);
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

  display.begin();  // brings up the whole board (see SpriteDisplay(LCDBoard&))

  display.addSprite(20, 20, 90, 90, green);
  display.addSprite(130, 20, 90, 90, blue);

  gestures.onGesture = onGesture;
  gestures.isDraggable = isOverSprite;
}

void loop() { gestures.update(*board.touch()); }
