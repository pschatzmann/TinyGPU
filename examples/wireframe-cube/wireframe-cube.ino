/**
 * @file wireframe-cube.ino
 * @brief Cross-platform TinyGPU WireFrame3D rotating cube demo - unchanged
 * source runs on both an ESP32 board and the SDL2 desktop backend, since
 * both are reached through the same LCDBoard interface (see LCDBoards.h,
 * which picks the right board class per platform automatically).
 *
 * On ESP32 this targets the ESP32 Cheap Yellow Display (ESP32-2432S028R)
 * via LCDBoardGuitionESP32_LVGL_2_4Display - swap the board type below for
 * a different LCDBoard if yours differs. On desktop it opens an SDL2
 * window the same size as that panel (240x320) via LCDBoardDesktopSDL.
 *
 * For an ESP32S3-specific variant that streams the cube as H264 video over
 * UDP instead of driving a local panel, see wireframe-cube-h264.ino.
 */
#include <Arduino.h>
#include "TinyGPU.h"
#include <TinyGPU/Boards/LCDBoards.h>

#ifdef ESP32
LCDBoardGuitionESP32_LVGL_2_4Display board;
#else
LCDBoardDesktopSDL board(240, 320);
#endif

FrameBufferRGB565 framebuffer(board.width(), board.height(), FontRGB565);
WireFrame3D_RGB565 wireframe(framebuffer);
auto cubeMesh = WireFrame3D_RGB565::cube(2.0f);
float angle = 0.0f;

void setup() {
  Serial.begin(115200);
  board.begin();

  framebuffer.begin();
  framebuffer.clear(RGB565(255, 255, 255));

  wireframe.begin();
  wireframe.setPerspective(60.0f, 0.1f, 100.0f);
  WireFrame3D_RGB565::Camera cam;
  cam.position = {0.0f, 0.0f, 5.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = {0.0f, 1.0f, 0.0f};
  wireframe.setCamera(cam);
}

void loop() {
  framebuffer.clear(RGB565(255, 255, 255));

  auto printer = framebuffer.linePrinter();
  printer.setColor(RGB565(0, 0, 255));
  printer.setScale(1);
  printer.print("Rotating Wireframe Cube Demo");

  // Build model matrix: rotate around Y and X
  auto model = WireFrame3D_RGB565::translation(0.0f, 0.0f, 0.0f) *
               WireFrame3D_RGB565::rotationY(angle) *
               WireFrame3D_RGB565::rotationX(angle * 0.7f);

  // Render the cube in black
  wireframe.renderWireframe(framebuffer, cubeMesh, model, RGB565(0, 0, 0));

  board.display().writeData(framebuffer);

  // rotation for next frame
  angle += 0.03f;
  delay(16);  // ~60 fps cap
}
