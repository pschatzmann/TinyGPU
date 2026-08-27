/**
 * @file sprite-display-demo.ino
 * @brief Draws two colored squares on a red background directly on the
 * display via SpriteDisplay (no framebuffer), and lets you drag them
 * around with touch/mouse input through GestureDetector.
 *
 * Runs unchanged on an ESP32 board or the SDL2 desktop backend, both
 * reached through the same LCDBoard interface (see LCDBoards.h).
 */
#include <Arduino.h>
#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>
#include <TinyGPU/Surface/SpriteDisplay.h>
#include <TinyGPU/Input/GestureDetector.h>

constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 320;

RGB565 red(255, 0, 0);
RGB565 green(0, 255, 0);
RGB565 blue(0, 0, 255);
RGB565 white(255, 255, 255);
RGB565 black(0, 0, 0);

#ifdef ESP32
LCDBoardGuitionESP32_LVGL_2_4Display board;
#else
LCDBoardDesktopSDL board(kDisplayWidth, kDisplayHeight);
#endif
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
  char logLine[96];
  snprintf(logLine, sizeof(logLine),
          "[Gesture] %s at (%d, %d) delta=(%d, %d) duration=%lums",
          toString(e.type), e.point.x, e.point.y, e.deltaX, e.deltaY,
          static_cast<unsigned long>(e.durationMs));
  Serial.println(logLine);

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
