/**
 * @file basic-example.ino
 * @brief Minimal TinyGPU example for ESP32/Arduino.
 *
 * This example demonstrates basic usage of the TinyGPU library:
 *   - Creates a Surface<RGB565> framebuffer
 *   - Clears the screen and draws a rectangle
 *   - Renders text to the framebuffer
 *   - Switches between the built-in 5x7 font and the fonts converted from
 *     STM32-EVAL's fonts.c (Font8x8, Font12x12, Font16x24) via setFont()
 *   - (Stub) sendFrameToDisplay() for hardware integration
 *
 * Adapt sendFrameToDisplay() to your display hardware.
 */

#include <TinyGPU.h>

Surface<RGB565> gpu(128, 96, FontRGB565);

// Bitmap fonts (see AsciiFonts.h). Kept as
// static instances since Surface::setFont() only stores a reference.
Font8x8RGB565 font8x8;
Font12x12RGB565 font12x12;
Font16x24RGB565 font16x24;

void sendFrameToDisplay(const Surface<RGB565>& gpu) {
  Serial.println("Frame ready to send to display:");
  // write your display code here, e.g.:
  // display.drawBitmap(0, 0, gpu.data(), gpu.width(), gpu.height
 }

void setup() {
  Serial.begin(115200);
  gpu.begin();
  gpu.clear(RGB565(0, 0, 0));
  gpu.drawRect(0, 0, 128, 96, RGB565(255, 255, 255));

  // Default 5x7 font (FontRGB565), set at construction time.
  gpu.drawText(4, 4, "Hello TinyGPU", RGB565(255, 255, 0));

  // Switch to the 8x8 font for a line of text.
  gpu.setFont(font8x8);
  gpu.drawText(4, 16, "Font8x8", RGB565(0, 255, 255));

  // Switch to the 12x12 font.
  gpu.setFont(font12x12);
  gpu.drawText(4, 30, "12x12", RGB565(0, 255, 0));

  // Switch to the 16x24 font for larger, more legible text.
  gpu.setFont(font16x24);
  gpu.drawText(4, 48, "16x24", RGB565(255, 128, 0));

  // Restore the default font for any further text.
  gpu.setFont(FontRGB565);
}

void loop() { sendFrameToDisplay(gpu); }
