/**
 * @file basic-example.ino
 * @brief Cross-platform TinyGPU example: draws framebuffer text/shapes,
 * decodes a tiny embedded BMP into a sprite, and animates a second sprite
 * (move/scale/rotate) - unchanged source runs on both an ESP32 board and
 * the SDL2 desktop backend, since both are reached through the same
 * LCDBoard interface (see LCDBoards.h, which picks the right board class
 * per platform automatically).
 *
 * On ESP32 this targets the ESP32 Cheap Yellow Display (ESP32-2432S028R)
 * via LCDBoardGuitionESP32_LVGL_2_4Display - swap the board type below for
 * a different LCDBoard if yours differs. On desktop it opens an SDL2
 * window the same size as that panel (240x320) via LCDBoardDesktopSDL.
 */
#include <Arduino.h>
#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>

#ifdef ESP32
LCDBoardGuitionESP32_LVGL_2_4Display board;
#else
LCDBoardDesktopSDL board(240, 320);
#endif

FrameBuffer<RGB565> framebuffer(board.width(), board.height(), FontRGB565);

// Bitmap fonts (see AsciiFonts.h). Kept as static instances since
// Surface::setFont() only stores a reference.
Font8x8RGB565 font8x8;
Font12x12RGB565 font12x12;
Font16x24RGB565 font16x24;

// --- BMP decode demo: a tiny embedded 2x2 BMP (one of the smallest valid
// BMP files - a magenta/blue/yellow/black 2x2 checkerboard), decoded via
// BMPParser and then upscaled 16x (to 32x32) with scaleSpriteImage() purely
// so the result is actually visible on screen - the BMP itself really is
// only 2x2 pixels, so without this upscale it would (correctly!) render as
// a near-invisible 2-pixel dot. ---
Surface<RGB565> bmpImage;
BMPParser<RGB565> bmpParser(bmpImage);
const uint8_t kBmpData[] = {
    0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00,
    0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00};
constexpr int kBmpDisplayScale = 16;

// --- Animated sprite: moves side to side while scaling/rotating, inside
// a clearly outlined lane so its motion is easy to spot on screen. ---
Sprite<RGB565> sprite(18, 18, FontRGB565);
SpriteInfo<RGB565, Surface<RGB565>>* spriteInfo = nullptr;
const size_t kLaneTop = 150;
const size_t kLaneHeight = 100;
const size_t kSpriteMinX = 8;
const size_t kSpriteMaxX = board.width() - 48;
const size_t kSpriteY = kLaneTop + (kLaneHeight - 40) / 2;
float scale = 1.0f;
float angle = 0.0f;
size_t positionX = kSpriteMinX;
int direction = 1;

void buildSprite() {
  sprite.begin();
  sprite.clear(RGB565(0, 0, 0));
  sprite.fillCircle(9, 9, 8, RGB565(255, 120, 0));
  sprite.drawLine(9, 2, 13, 9, RGB565(255, 255, 255));
  sprite.drawLine(13, 9, 9, 16, RGB565(255, 255, 255));
  sprite.drawLine(9, 16, 5, 9, RGB565(255, 255, 255));
  sprite.drawLine(5, 9, 9, 2, RGB565(255, 255, 255));
}

void setup() {
  Serial.begin(115200);
  board.begin();

  framebuffer.begin();
  framebuffer.clear(RGB565(0, 0, 0));
  framebuffer.drawRect(0, 0, framebuffer.width(), framebuffer.height(),
                       RGB565(0, 80, 160));

  // Default 5x7 font (FontRGB565), set at construction time.
  framebuffer.drawText(6, 6, "TinyGPU demo", RGB565(255, 255, 0));

  // Switch to the 8x8 font for a line of text.
  framebuffer.setFont(font8x8);
  framebuffer.drawText(6, 20, "Font8x8", RGB565(0, 255, 255));

  // Switch to the 12x12 font.
  framebuffer.setFont(font12x12);
  framebuffer.drawText(6, 36, "12x12", RGB565(0, 255, 0));

  // Switch to the 16x24 font for larger, more legible text.
  framebuffer.setFont(font16x24);
  framebuffer.drawText(6, 56, "16x24", RGB565(255, 128, 0));

  // Restore the default font for any further text.
  framebuffer.setFont(FontRGB565);

  // Decode the embedded BMP (a real 2x2 BMP file) and draw it upscaled
  // 16x so the 4 decoded pixels are actually visible as distinct squares.
  bmpParser.write(kBmpData, sizeof(kBmpData));
  if (bmpParser.isComplete()) {
    Surface<RGB565> bmpUpscaled =
        scaleSpriteImage(bmpImage, static_cast<float>(kBmpDisplayScale),
                         font8x8);
    framebuffer.drawSprite(6, 90, bmpUpscaled);
    framebuffer.drawText(46, 100, "<- decoded 2x2 BMP,", RGB565(255, 255, 255));
    framebuffer.drawText(46, 112, "   shown 16x upscaled", RGB565(255, 255, 255));
  } else if (bmpParser.hasError()) {
    framebuffer.drawText(6, 90, "BMP decode failed", RGB565(255, 0, 0));
  }

  // Outline the lane the animated sprite moves in below, so its motion is
  // easy to spot against the rest of the screen.
  framebuffer.drawText(6, kLaneTop - 12, "Moving sprite:", RGB565(0, 200, 255));
  framebuffer.drawRect(4, kLaneTop, framebuffer.width() - 8, kLaneHeight,
                       RGB565(60, 60, 60));

  // Add the animated sprite last so its saved "background" snapshot
  // doesn't include a not-yet-drawn version of itself.
  buildSprite();
  spriteInfo = &framebuffer.addSprite(positionX, kSpriteY, 40, 40, sprite,
                                      RGB565(0, 0, 0));

  // Send the initial framebuffer to the display.
  board.display().writeData(framebuffer);
}

void loop() {
  if (spriteInfo == nullptr) return;

  positionX = static_cast<size_t>(static_cast<int>(positionX) + direction);
  if (positionX > kSpriteMaxX) {
    direction = -1;
  } else if (positionX < kSpriteMinX) {
    direction = 1;
  }
  framebuffer.moveSprite(*spriteInfo, positionX, kSpriteY);

  scale += 0.04f * static_cast<float>(direction);
  scale = constrain(scale, 0.8f, 1.3f);
  framebuffer.scaleSprite(*spriteInfo, scale);

  angle += 8.0f;
  if (angle >= 360.0f) {
    angle -= 360.0f;
  }
  framebuffer.rotateSprite(*spriteInfo, angle);

  board.display().writeData(framebuffer);
  delay(16);  // ~60 fps cap - without it the loop spins as fast as the CPU
              // allows, which just wastes cycles/floods Serial for no
              // visible benefit.
}
