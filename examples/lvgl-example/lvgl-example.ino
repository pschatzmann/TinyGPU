/**
 * @file lvgl-example.ino
 * @brief TinyGPU LVGL v9 dashboard example for a 3.5" 240x320 ILI9341 SPI
 * TFT touch board, e.g. the "ESP32 LVGL WiFi & Bluetooth" development
 * boards sold with a 3.5" 240x320 touch display.
 *
 * Demonstrates a small interactive dashboard built entirely with LVGL
 * widgets running on top of TinyGPU's LVGLDriver:
 *  - an animated arc gauge driven by a periodic LVGL timer (simulated
 *    sensor reading)
 *  - a slider that drives the display's actual backlight brightness via
 *    PWM, live, as you drag it
 *  - a switch that toggles an LED indicator
 *  - a button with a tap counter
 * All of it is touch-interactive through the CST816S capacitive touch
 * controller wired into LVGL's input device system by LVGLDriver.
 *
 */
#include <TinyGPU.h>
#include <TinyGPU/DisplayDriverSPI.h>
#include <TinyGPU/LVGLDriver.h>
#include <TinyGPU/RBG565.h>
#include <lvgl.h>
#include <math.h>

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

// --- Touch I2C Pins (CST816S capacitive touch) -----------------------------
constexpr int8_t kPinTouchSda = 33;
constexpr int8_t kPinTouchScl = 32;
constexpr int8_t kPinTouchIrq = 36;

// Explicit buffer allocation size (320 * 20 lines * 2 bytes)
constexpr size_t kLvglBufferSize = kDisplayWidth * 2;
ILI9341Driver<RBG565> tftDriver(SPI, kPinCs, kPinDc, kPinRst);
LVGLDriver<RBG565> lvglDriver(tftDriver, kDisplayWidth, kDisplayHeight,
                              kLvglBufferSize);
TouchDriverCST816S touchDriver(Wire, /*rstPin=*/-1, kPinTouchIrq);

// --- dashboard widgets ------------------------------------------------------
lv_obj_t* gaugeArc = nullptr;
lv_obj_t* gaugeLabel = nullptr;
lv_obj_t* sliderLabel = nullptr;
lv_obj_t* counterLabel = nullptr;
lv_obj_t* alertLed = nullptr;
int tapCount = 0;
float sensorPhase = 0.0f;

// Periodic LVGL timer: feeds the arc gauge a simulated sensor reading.
void updateSensor(lv_timer_t* timer) {
  sensorPhase += 0.05f;
  int value = static_cast<int>((sinf(sensorPhase) * 0.5f + 0.5f) * 100.0f);
  lv_arc_set_value(gaugeArc, value);
  lv_label_set_text_fmt(gaugeLabel, "%d°C", value);
}

// Slider drag: drives the panel's real backlight brightness via PWM.
void onSliderChanged(lv_event_t* e) {
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  int32_t value = lv_slider_get_value(slider);
  lv_label_set_text_fmt(sliderLabel, "Backlight: %ld%%", static_cast<long>(value));
  ledcWrite(kPinBacklight, map(value, 0, 100, 0, 255));
}

void onSwitchToggled(lv_event_t* e) {
  lv_obj_t* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
  if (lv_obj_has_state(sw, LV_STATE_CHECKED)) {
    lv_led_on(alertLed);
  } else {
    lv_led_off(alertLed);
  }
}

void onButtonTapped(lv_event_t* e) {
  tapCount++;
  lv_label_set_text_fmt(counterLabel, "Taps: %d", tapCount);
}

void buildDashboard() {
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

  // --- title ---
  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "TinyGPU Dashboard");
  lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

  // --- gauge arc (simulated sensor reading) ---
  gaugeArc = lv_arc_create(screen);
  lv_obj_set_size(gaugeArc, 160, 160);
  lv_arc_set_range(gaugeArc, 0, 100);
  lv_arc_set_bg_angles(gaugeArc, 135, 45);
  lv_arc_set_rotation(gaugeArc, 0);
  lv_arc_set_value(gaugeArc, 0);
  lv_obj_remove_style(gaugeArc, nullptr, LV_PART_KNOB | LV_STATE_ANY);
  lv_obj_clear_flag(gaugeArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(gaugeArc, 14, LV_PART_MAIN);
  lv_obj_set_style_arc_color(gaugeArc, lv_color_hex(0x2A2F35), LV_PART_MAIN);
  lv_obj_set_style_arc_width(gaugeArc, 14, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(gaugeArc, lv_palette_main(LV_PALETTE_ORANGE), LV_PART_INDICATOR);
  lv_obj_align(gaugeArc, LV_ALIGN_TOP_MID, 0, 40);

  gaugeLabel = lv_label_create(screen);
  lv_label_set_text(gaugeLabel, "0\xC2\xB0"
                                "C");
  lv_obj_set_style_text_color(gaugeLabel, lv_color_white(), LV_PART_MAIN);
  lv_obj_align_to(gaugeLabel, gaugeArc, LV_ALIGN_CENTER, 0, 10);

  // --- slider (real backlight brightness) ---
  lv_obj_t* slider = lv_slider_create(screen);
  lv_obj_set_width(slider, 200);
  lv_slider_set_range(slider, 10, 100);
  lv_slider_set_value(slider, 100, LV_ANIM_OFF);
  lv_obj_align(slider, LV_ALIGN_TOP_MID, 0, 232);
  lv_obj_set_style_bg_color(slider, lv_palette_main(LV_PALETTE_BLUE), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, lv_palette_main(LV_PALETTE_BLUE), LV_PART_KNOB);
  lv_obj_add_event_cb(slider, onSliderChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  sliderLabel = lv_label_create(screen);
  lv_label_set_text(sliderLabel, "Backlight: 100%");
  lv_obj_set_style_text_color(sliderLabel, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
  lv_obj_align_to(sliderLabel, slider, LV_ALIGN_OUT_TOP_MID, 0, -8);

  // --- switch + LED indicator ---
  lv_obj_t* sw = lv_switch_create(screen);
  lv_obj_align(sw, LV_ALIGN_TOP_LEFT, 24, 268);
  lv_obj_add_event_cb(sw, onSwitchToggled, LV_EVENT_VALUE_CHANGED, nullptr);

  alertLed = lv_led_create(screen);
  lv_obj_set_size(alertLed, 20, 20);
  lv_led_set_color(alertLed, lv_palette_main(LV_PALETTE_RED));
  lv_led_off(alertLed);
  lv_obj_align_to(alertLed, sw, LV_ALIGN_OUT_RIGHT_MID, 16, 0);

  // --- button + tap counter ---
  lv_obj_t* btn = lv_button_create(screen);
  lv_obj_set_size(btn, 90, 40);
  lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -20, 262);
  lv_obj_set_style_bg_color(btn, lv_palette_main(LV_PALETTE_TEAL), LV_PART_MAIN);
  lv_obj_add_event_cb(btn, onButtonTapped, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* btnLabel = lv_label_create(btn);
  lv_label_set_text(btnLabel, "Tap me");
  lv_obj_center(btnLabel);

  counterLabel = lv_label_create(screen);
  lv_label_set_text(counterLabel, "Taps: 0");
  lv_obj_set_style_text_color(counterLabel, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
  lv_obj_align_to(counterLabel, btn, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

  lv_timer_create(updateSensor, 80, nullptr);
}

void my_lvgl_log(const char* buf) { Serial.print(buf); }

void setup() {
  Serial.begin(115200);
  Serial.println("starting...");

  ledcAttach(kPinBacklight, 5000, 8);
  ledcWrite(kPinBacklight, 255);

  SPI.begin(kPinSclk, kPinMiso, kPinMosi, kPinCs);
  Wire.begin(kPinTouchSda, kPinTouchScl);
  lvglDriver.setTouchDriver(touchDriver);

  if (!lvglDriver.begin()) {
    Serial.println("LVGL Driver initialization failed!");
    while (1);
  }

  // lvglDriver.setGamma(1.0f, 1.0f, 1.0f);

  buildDashboard();

  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();
  lvglDriver.delay(5);
}
