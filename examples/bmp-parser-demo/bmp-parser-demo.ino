/**
 * @file bmp-parser-demo.ino
 * @brief TinyGPU BMP parser demo for ESP32/Arduino.
 *
 * This example demonstrates:
 *   - Creating a framebuffer and image surface
 *   - Parsing a BMP image from a byte array using BMPParser
 *   - Drawing the decoded image and status text to the framebuffer
 *   - (Stub) sendFrameToDisplay() for hardware integration
 *
 * Adapt sendFrameToDisplay() to your display hardware.
 */

#include <TinyGPU.h>

constexpr size_t kDisplayWidth = 64;
constexpr size_t kDisplayHeight = 64;

Surface<RGB565> framebuffer(kDisplayWidth, kDisplayHeight, FontRGB565);
Surface<RGB565> image;
BMPParser<RGB565> parser(image);

const uint8_t kBmpData[] = {
    0x42, 0x4D, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x00,
    0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x13, 0x0B, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    0x00, 0x00, 0xFF, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00};

void sendFrameToDisplay(const Surface<RGB565>& gpu) {
  Serial.println("Frame ready to send to display:");
  // write your display code here, e.g.:
  // display.drawBitmap(0, 0, gpu.data(), gpu.width(), gpu.height
}

void setup() {
  Serial.begin(115200);

  framebuffer.begin();
  framebuffer.clear(RGB565(0, 0, 0));
  framebuffer.drawRect(0, 0, framebuffer.width(), framebuffer.height(),
                       RGB565(0, 80, 160));
  framebuffer.drawText(4, 4, "BMP parser demo", RGB565(255, 255, 0));

  parser.write(kBmpData, sizeof(kBmpData));
  if (parser.isComplete()) {
    framebuffer.drawSprite(20, 20, image);
    framebuffer.drawText(4, 50, "Decoded 2x2 BMP", RGB565(255, 255, 255));
  } else if (parser.hasError()) {
    framebuffer.drawText(4, 50, "BMP decode failed", RGB565(255, 0, 0));
  }

  sendFrameToDisplay(framebuffer);
}

void loop() {}
