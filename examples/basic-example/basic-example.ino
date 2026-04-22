/**
 * @file basic-example.ino
 * @brief Minimal TinyGPU example for ESP32/Arduino.
 *
 * This example demonstrates basic usage of the TinyGPU library:
 *   - Creates a Surface<RGB565> framebuffer
 *   - Clears the screen and draws a rectangle
 *   - Renders text to the framebuffer
 *   - (Stub) sendFrameToDisplay() for hardware integration
 *
 * Adapt sendFrameToDisplay() to your display hardware.
 */

#include <TinyGPU.h>

Surface<RGB565> gpu(128, 64, FontRGB565);

void sendFrameToDisplay(const Surface<RGB565>& gpu) {
  Serial.println("Frame ready to send to display:");
  // write your display code here, e.g.:
  // display.drawBitmap(0, 0, gpu.data(), gpu.width(), gpu.height
 }

void setup() {
  Serial.begin(115200);
  gpu.begin();
  gpu.clear(RGB565(0, 0, 0));
  gpu.drawRect(0, 0, 128, 64, RGB565(255, 255, 255));
  gpu.drawText(4, 4, "Hello TinyGPU", RGB565(255, 255, 0));
}

void loop() { sendFrameToDisplay(gpu); }
