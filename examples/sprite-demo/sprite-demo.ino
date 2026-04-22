/**
 * @file sprite-demo.ino
 * @brief TinyGPU sprite animation demo for ESP32/Arduino.
 *
 * This example demonstrates:
 *   - Creating a framebuffer and a sprite surface
 *   - Drawing and animating a sprite (move, scale, rotate)
 *   - Using FrameBuffer's sprite management API
 *   - (Stub) sendFrameToDisplay() for hardware integration
 *
 * Adapt sendFrameToDisplay() to your display hardware.
 */
#include <TinyGPU.h>

constexpr size_t kDisplayWidth = 128;
constexpr size_t kDisplayHeight = 64;

FrameBuffer<RGB565> framebuffer(kDisplayWidth, kDisplayHeight, FontRGB565);
Surface<RGB565> sprite(18, 18, FontRGB565);
FrameBuffer<RGB565>::SpriteInfo* spriteInfo = nullptr;
float scale = 1.0f;
float angle = 0.0f;
size_t positionX = 8;
size_t positionY = 18;
int direction = 1;

void sendFrameToDisplay(const ISurface<RGB565>& gpu) { (void)gpu; }

void buildSprite() {
  sprite.clear(RGB565(0, 0, 0));
  sprite.fillCircle(9, 9, 8, RGB565(255, 120, 0));
  sprite.drawLine(9, 2, 13, 9, RGB565(255, 255, 255));
  sprite.drawLine(13, 9, 9, 16, RGB565(255, 255, 255));
  sprite.drawLine(9, 16, 5, 9, RGB565(255, 255, 255));
  sprite.drawLine(5, 9, 9, 2, RGB565(255, 255, 255));
}

void setup() {
  Serial.begin(115200);

  framebuffer.begin();
  framebuffer.clear(RGB565(0, 0, 0));
  framebuffer.drawRect(0, 0, framebuffer.width(), framebuffer.height(),
                       RGB565(0, 80, 160));
  framebuffer.drawText(6, 6, "Sprite demo", RGB565(255, 255, 0));

  buildSprite();
  spriteInfo = &framebuffer.addSprite(positionX, positionY, sprite, RGB565(0, 0, 0));
  sendFrameToDisplay(framebuffer);
}

void loop() {
  if (spriteInfo == nullptr) {
    return;
  }

  positionX = static_cast<size_t>(static_cast<int>(positionX) + direction);
  if (positionX > 92) {
    direction = -1;
  } else if (positionX < 8) {
    direction = 1;
  }

  framebuffer.moveSprite(*spriteInfo, positionX, positionY);

  scale += 0.04f * static_cast<float>(direction);
  if (scale > 1.8f) {
    scale = 1.8f;
  } else if (scale < 0.8f) {
    scale = 0.8f;
  }
  framebuffer.scaleSprite(*spriteInfo, scale);

  angle += 8.0f;
  if (angle >= 360.0f) {
    angle -= 360.0f;
  }
  framebuffer.rotateSprite(*spriteInfo, angle);

  sendFrameToDisplay(framebuffer);
}
