/**
 * @file desktop.ino
 * @brief TinyGPU desktop example using SDL2 for display output.
 * This example demonstrates how to use the TinyGPU library with SDL2 to create
 * a simple graphical application on desktop platforms. It initializes a
 * framebuffer, creates a sprite, and animates the sprite across the screen
 * while applying scaling and rotation transformations.
 */
#include <TinyGPU.h>
#include <TinyGPU/DisplayDriverSDL.h>

constexpr size_t kDisplayWidth = 128;
constexpr size_t kDisplayHeight = 64;

FrameBuffer<RGB565> framebuffer(kDisplayWidth, kDisplayHeight, FontRGB565);
Sprite<RGB565> sprite(18, 18, FontRGB565);
DisplayDriverSDL driver(kDisplayWidth, kDisplayHeight);
DeviceOutput display(driver);

float scale = 1.0f;
float angle = 0.0f;
size_t positionX = 8;
size_t positionY = 18;
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

  display.begin();

  framebuffer.begin();
  framebuffer.clear(RGB565(0, 0, 0));
  framebuffer.drawRect(0, 0, framebuffer.width(), framebuffer.height(),
                       RGB565(0, 80, 160));
  framebuffer.drawText(6, 6, "Sprite demo", RGB565(255, 255, 0));

  buildSprite();
  SpriteInfo<RGB565, Surface<RGB565>>& spriteInfo = framebuffer.addSprite(positionX, positionY, 40, 40, sprite,
                                      RGB565(0, 0, 0)); 

  // Send the initial framebuffer to the display
  display.writeData(framebuffer);
}

void loop() {
  SpriteInfo<RGB565, Surface<RGB565>>* spriteInfo = framebuffer.getSpritePtr(0);  // Assuming only one sprite is added for this demo
  if (spriteInfo == nullptr) {
    Serial.println("No sprite found.");
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
  scale = constrain(scale, 0.8f, 1.3f);
  framebuffer.scaleSprite(*spriteInfo, scale);

  angle += 8.0f;
  if (angle >= 360.0f) {
    angle -= 360.0f;
  }
  framebuffer.rotateSprite(*spriteInfo, angle);

  // Send the updated framebuffer to the display
  display.writeData(framebuffer);
}
