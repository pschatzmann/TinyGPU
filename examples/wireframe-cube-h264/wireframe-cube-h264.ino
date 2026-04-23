/**
 * @file wireframe-cube-h264.ino
 * @brief TinyGPU WireFrame3D rotating cube demo.
 *
 * This example demonstrates drawing a rotating 3D wireframe cube
 * using the TinyGPU library's WireFrame3D class and sends a H264-encoded video
 * stream over UDP.
 *
 * You watch the result using ffmpeg by calling "ffplay -f h264 udp://@:5000" 
 * It takes a few seconds for the stream to start, so be patient after running the ESP32S3 code.
 */

#include "H264Encoder.h"  // https://pschatzmann.github.io/ESP32S3-h264/
#include "TinyGPU.h"
#include "UDPPrint.h"

// UDP destination for streaming the framebuffer (optional)
const char* DEST_IP = "192.168.1.44";
const uint16_t DEST_PORT = 5000;

// Framebuffer size VGA
constexpr int WIDTH = 600;
constexpr int HEIGHT = 400;

// Output
H264Encoder encoder;
UDPPrint udp;

// Create framebuffer and wireframe objects
FrameBufferRGB565 framebuffer(WIDTH, HEIGHT, FontRGB565);
WireFrame3D_RGB565 wireframe(framebuffer);

// Use the built-in cube mesh helper
auto cubeMesh = WireFrame3D_RGB565::cube(100.0f);

float angle = 0.0f;

void setup() {
  Serial.begin(115200);

  auto encoderConfig = encoder.defaultConfig();
  encoderConfig.width = WIDTH;
  encoderConfig.height = HEIGHT;
  encoderConfig.fps = 30;
  encoderConfig.ssid = "YourWiFiSSID";  // Set your WiFi SSID
  encoderConfig.password = "YourWiFiPassword";  // Set your WiFi password
  encoder.begin(encoderConfig);


  framebuffer.begin();
  framebuffer.clear(RGB565(255, 255, 255));

  wireframe.begin();


  udp.begin(DEST_IP, DEST_PORT);
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
  encoder.encodeRGB565(framebuffer.data(), framebuffer.size(), udp);
  Serial.print("Sending bytes: ");
  Serial.println(framebuffer.size());

  // rotation for next frame
  angle += 0.03f;
}
