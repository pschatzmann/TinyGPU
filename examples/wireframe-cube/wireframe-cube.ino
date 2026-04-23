/**
 * @file wireframe-cube.ino
 * @brief TinyGPU WireFrame3D rotating cube demo.
 *
 * This example demonstrates drawing a rotating 3D wireframe cube
 * using the TinyGPU library's WireFrame3D class.
 */

#include "TinyGPU.h"

// Framebuffer size VGA
constexpr int WIDTH = 600;
constexpr int HEIGHT = 400;

// Create framebuffer and wireframe objects
FrameBufferRGB565 framebuffer(WIDTH, HEIGHT, FontRGB565);
WireFrame3D_RGB565 wireframe(framebuffer);
auto cubeMesh = WireFrame3D_RGB565::cube(100.0f);
float angle = 0.0f;

void sendFrameToDisplay(const ISurface<RGB565>& gpu) {
  Serial.println("Frame ready to send to display:");
  // write your display code here, e.g.:
  // display.drawBitmap(0, 0, gpu.data(), gpu.width(), gpu.height
}

void setup() {
  Serial.begin(115200);

  framebuffer.begin();
  framebuffer.clear(RGB565(255, 255, 255));

  wireframe.begin();
}

void loop() {
  framebuffer.clear(RGB565(255, 255, 255));

  auto printer = framebuffer.linePrinter();
  printer.setColor(RGB565(0, 0, 255));
  printer.setScale(10);
  printer.print("Rotating Wireframe Cube Demo");

  // Set up camera and projection
  wireframe.setPerspective(60.0f, 0.1f, 100.0f);
  WireFrame3D_RGB565::Camera cam;
  cam.position = {0.0f, 0.0f, 5.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = {0.0f, 1.0f, 0.0f};
  wireframe.setCamera(cam);

  // Build model matrix: rotate around Y and X
  auto model = WireFrame3D_RGB565::translation(0.0f, 0.0f, 0.0f) *
               WireFrame3D_RGB565::rotationY(angle) *
               WireFrame3D_RGB565::rotationX(angle * 0.7f);

  // Render the cube
  wireframe.renderWireframe(framebuffer, cubeMesh, model,
                            RGB565(255, 255, 255));

  // output via UDP
  sendFrameToDisplay(framebuffer);

  // rotation for next frame
  angle += 0.03f;
}
