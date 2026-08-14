/**
 * @file lvgl-example.ino
 * @brief TinyGPU LVGL v9 example for a 3.5" 240x320 ILI9341 SPI TFT touch
 * board, e.g. the "ESP32 LVGL WiFi & Bluetooth" development boards sold with
 * a 3.5" 240x320 touch display.
 * 
 * We use the LVGL library to create a simple GUI with a red box on a black background. 
 */
#include <TinyGPU.h>
#include <TinyGPU/DisplayDriverSPI.h>
#include <TinyGPU/LVGLDriver.h>
#include <lvgl.h>

// --- display geometry (Landscape) ----------------------------------------
constexpr int kDisplayWidth = 240;   // 320;  // Swapped for Landscape
constexpr int kDisplayHeight = 320;  // 240;

// --- SPI / display pins ---------------------------------------------------
constexpr int8_t kPinMosi = 13;
constexpr int8_t kPinMiso = 12;
constexpr int8_t kPinSclk = 14;
constexpr int8_t kPinCs = 15;
constexpr int8_t kPinDc = 2;
constexpr int8_t kPinRst = -1;
constexpr int8_t kPinBacklight = 27;

// Explicit buffer allocation size (320 * 20 lines * 2 bytes)
constexpr size_t kLvglBufferSize = kDisplayWidth * 2;
ILI9341Driver tftDriver(SPI, kPinCs, kPinDc, kPinRst);
LVGLDriver lvglDriver(tftDriver, kDisplayWidth, kDisplayHeight,
                      kLvglBufferSize);

void clearScreen() {
  Serial.println("clearScreen");
  lv_obj_t* screen = lv_screen_active();

  lv_obj_clean(screen);

  lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN);

  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
}

void drawBox() {
  Serial.println("drawBox");
  lv_obj_t* box = lv_obj_create(lv_screen_active());

  lv_obj_set_size(box, 100, 100);
  lv_obj_center(box);

  lv_obj_set_style_bg_color(box, lv_color_hex(0xFF0000), LV_PART_MAIN);

  lv_obj_set_style_border_width(box, 0, LV_PART_MAIN);
}

void my_lvgl_log(const char* buf) { Serial.print(buf); }

void setup() {
  Serial.begin(115200);
  Serial.println("starting...");

  pinMode(kPinBacklight, OUTPUT);
  digitalWrite(kPinBacklight, HIGH);

  SPI.begin(kPinSclk, kPinMiso, kPinMosi, kPinCs);

  if (!lvglDriver.begin()) {
    Serial.println("LVGL Driver initialization failed!");
    while (1);
  }

  // Create UI Label
  //   lv_obj_t* label = lv_label_create(lv_screen_active());
  //   lv_label_set_text(label, "Hello Arduino, I'm LVGL!");
  //   lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

  clearScreen();
  drawBox();

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();
  lvglDriver.delay(5);
}