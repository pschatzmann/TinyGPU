/**
 * @file bouncing-ball.ino
 * @brief TinyGPU bouncing ball demo for a 3.5" 240x320 ILI9341 SPI TFT
 * touch board, e.g. the "ESP32 LVGL WiFi & Bluetooth" development boards
 * sold with a 3.5" 240x320 touch display.
 *
 * A full 240x320 RGB565 framebuffer needs ~150 KB in one contiguous
 * allocation, which a classic ESP32 without PSRAM often can't satisfy even
 * with plenty of total free heap, because its internal DRAM is split into
 * several smaller non-contiguous pools. On top of that, redrawing and
 * transmitting the whole screen every frame is wasteful anyway: only the
 * small area the ball actually touches ever changes.
 *
 * So this sketch uses two small buffers instead of one big framebuffer:
 *  - `band`: used once at startup to draw the full screen (background +
 *    border) in 240x40 horizontal strips.
 *  - `spriteWindow`: a small 40x40 buffer reused every frame afterwards.
 *    Each frame it's redrawn with the correct background/border for its
 *    current screen position plus the ball at its new location, then sent
 *    to the one small rectangle of the screen that covers both the ball's
 *    old and new position - a sprite-style "dirty rect" update instead of
 *    a full-screen redraw. That cuts per-frame SPI traffic from ~150 KB to
 *    a few KB.
 *
 *   TFT_MOSI -> GPIO13   TFT_MISO -> GPIO12   TFT_SCLK -> GPIO14
 *   TFT_CS   -> GPIO15   TFT_DC   -> GPIO2    TFT_RST  -> not connected (-1)
 *   TFT_BL   -> GPIO27 (backlight)
 *
 * If the screen stays blank: check the backlight pin and SPI pins first.
 * If it lights up but shows garbage/wrong colors/shifted image: see the
 * troubleshooting notes below setup().
 */

#include <TinyGPU.h>
#include <TinyGPU/DisplayDriverSPI.h>

// --- display geometry ---------------------------------------------------
constexpr int kDisplayWidth = 240;
constexpr int kDisplayHeight = 320;

// --- initial full-screen draw geometry ------------------------------------
// 240 x 40 x 2 bytes = 19,200 bytes per band, well under any single-alloc
// ceiling. kDisplayHeight must be evenly divisible by kBandHeight.
constexpr int kBandHeight = 40;
constexpr int kBandCount = kDisplayHeight / kBandHeight;

// --- per-frame sprite window geometry --------------------------------------
// Big enough to always contain both the ball's previous and new position
// (diameter 28 + up to ~3px of motion per frame, plus safety margin).
// 40 x 40 x 2 bytes = 3,200 bytes.
constexpr int kSpriteSize = 40;

// --- SPI / display pins ---------------------------------------------------
constexpr int8_t kPinMosi = 13;
constexpr int8_t kPinMiso = 12;
constexpr int8_t kPinSclk = 14;
constexpr int8_t kPinCs = 15;
constexpr int8_t kPinDc = 2;
constexpr int8_t kPinRst = -1;
constexpr int8_t kPinBacklight = 27;

SurfaceRGB565 band(kDisplayWidth, kBandHeight, FontRGB565);
SurfaceRGB565 spriteWindow(kSpriteSize, kSpriteSize, FontRGB565);
ILI9341Driver tftDriver(SPI, kPinCs, kPinDc, kPinRst);

// --- ball state -----------------------------------------------------------
constexpr float kBallRadius = 14.0f;
const RGB565 kBallColor(255, 0, 0);
const RGB565 kBackgroundColor(0, 0, 0);
const RGB565 kBorderColor(0, 80, 160);

float ballX = kDisplayWidth / 2.0f;
float ballY = kDisplayHeight / 2.0f;
float velocityX = 2.6f;
float velocityY = 1.9f;

// Renders and sends one horizontal band of the screen, drawing whichever
// part of the border/ball falls within that band's y range. Only used for
// the one-time initial full-screen draw.
void renderBand(int bandIndex) {
  const int bandStartY = bandIndex * kBandHeight;
  const int bandEndY = bandStartY + kBandHeight - 1;

  band.clear(kBackgroundColor);

  // left/right border edges run the full screen height, so every band
  // draws its own local segment of them.
  band.drawLine(0, 0, 0, kBandHeight - 1, kBorderColor);
  band.drawLine(kDisplayWidth - 1, 0, kDisplayWidth - 1, kBandHeight - 1,
               kBorderColor);
  if (bandIndex == 0) {
    band.drawLine(0, 0, kDisplayWidth - 1, 0, kBorderColor);
  }
  if (bandIndex == kBandCount - 1) {
    band.drawLine(0, kBandHeight - 1, kDisplayWidth - 1, kBandHeight - 1,
                 kBorderColor);
  }

  // draw the ball only if it can possibly touch this band.
  if (ballY + kBallRadius >= bandStartY && ballY - kBallRadius <= bandEndY) {
    int localY = static_cast<int>(ballY) - bandStartY;
    band.fillCircle(static_cast<size_t>(ballX), static_cast<size_t>(localY),
                    static_cast<size_t>(kBallRadius), kBallColor);
  }

  tftDriver.writeData(band, 0, bandStartY);
}


void renderFullScreen() {
  for (int i = 0; i < kBandCount; ++i) {
    renderBand(i);
  }
}

// Redraws just the small screen rectangle the ball is moving through:
// background/border for that rectangle's current screen position, plus the
// ball at its new location. oldX/oldY is where the ball was drawn before
// this call, so the window is sized to cover both positions.
void renderBallWindow(float oldX, float oldY) {
  float midX = (oldX + ballX) / 2.0f;
  float midY = (oldY + ballY) / 2.0f;
  int windowX = constrain(static_cast<int>(midX) - kSpriteSize / 2, 0,
                          kDisplayWidth - kSpriteSize);
  int windowY = constrain(static_cast<int>(midY) - kSpriteSize / 2, 0,
                          kDisplayHeight - kSpriteSize);

  spriteWindow.clear(kBackgroundColor);

  // draw whichever screen border edges fall inside this window, in the
  // window's local coordinates.
  int leftX = -windowX;
  int rightX = (kDisplayWidth - 1) - windowX;
  int topY = -windowY;
  int bottomY = (kDisplayHeight - 1) - windowY;
  if (leftX >= 0 && leftX < kSpriteSize) {
    spriteWindow.drawLine(leftX, 0, leftX, kSpriteSize - 1, kBorderColor);
  }
  if (rightX >= 0 && rightX < kSpriteSize) {
    spriteWindow.drawLine(rightX, 0, rightX, kSpriteSize - 1, kBorderColor);
  }
  if (topY >= 0 && topY < kSpriteSize) {
    spriteWindow.drawLine(0, topY, kSpriteSize - 1, topY, kBorderColor);
  }
  if (bottomY >= 0 && bottomY < kSpriteSize) {
    spriteWindow.drawLine(0, bottomY, kSpriteSize - 1, bottomY, kBorderColor);
  }

  int localBallX = static_cast<int>(ballX) - windowX;
  int localBallY = static_cast<int>(ballY) - windowY;
  spriteWindow.fillCircle(static_cast<size_t>(localBallX),
                          static_cast<size_t>(localBallY),
                          static_cast<size_t>(kBallRadius), kBallColor);

  tftDriver.writeData(spriteWindow, windowX, windowY);
}

void setup() {
  Serial.begin(115200);

  if (kPinBacklight >= 0) {
    pinMode(kPinBacklight, OUTPUT);
    digitalWrite(kPinBacklight, HIGH);
  }

  SPI.begin(kPinSclk, kPinMiso, kPinMosi, kPinCs);
  tftDriver.begin();

  band.begin();
  spriteWindow.begin();
  renderFullScreen();
}

void loop() {
  float oldX = ballX;
  float oldY = ballY;

  // move the ball
  ballX += velocityX;
  ballY += velocityY;

  // bounce off the screen edges
  if (ballX - kBallRadius <= 0) {
    ballX = kBallRadius;
    velocityX = -velocityX;
  } else if (ballX + kBallRadius >= kDisplayWidth) {
    ballX = kDisplayWidth - kBallRadius;
    velocityX = -velocityX;
  }
  if (ballY - kBallRadius <= 0) {
    ballY = kBallRadius;
    velocityY = -velocityY;
  } else if (ballY + kBallRadius >= kDisplayHeight) {
    ballY = kDisplayHeight - kBallRadius;
    velocityY = -velocityY;
  }

  renderBallWindow(oldX, oldY);
  delay(5);  // cap the frame rate; the dirty-rect update is fast enough
             // to otherwise run much faster than is visually useful.
}
