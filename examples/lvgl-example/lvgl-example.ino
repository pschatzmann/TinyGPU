/**
 * @file lvgl-example.ino
 * @brief Cross-platform TinyGPU LVGL v9 dashboard example - unchanged
 * source runs on both an ESP32 board and the SDL2 desktop backend, since
 * both are reached through the same LCDBoard interface (see LCDBoards.h,
 * which picks the right board class per platform automatically) and
 * LVGLDriver accepts any DisplayDriver<RGB_T>, not just an SPI one.
 *
 * On ESP32 this targets the ESP32 Cheap Yellow Display (ESP32-2432S028R),
 * a 240x320 ILI9341 SPI TFT board - built up via the
 * LCDBoardGuitionESP32_LVGL_2_4Display board class (LCDBoardsESP32.h), so
 * its pin wiring doesn't need to be repeated here. Swap the board type
 * below for a different LCDBoard if yours differs. On desktop it opens an
 * SDL2 window of the same size via LCDBoardDesktopSDL.
 *
 * Demonstrates a small interactive dashboard built entirely with LVGL
 * widgets running on top of TinyGPU's LVGLDriver:
 *  - an animated arc gauge driven by a periodic LVGL timer (simulated
 *    sensor reading)
 *  - a slider that drives the display's actual backlight brightness via
 *    PWM on ESP32 (desktop has no backlight, so it just updates the label)
 *  - a switch that toggles an LED indicator
 *  - a button with a tap counter
 * All of it is touch-interactive: on ESP32 through the CST816S capacitive
 * touch controller, on desktop through the mouse (see TouchDriverSDL) -
 * both wired into LVGL's input device system by LVGLDriver.
 */
#include <Arduino.h>
#include <TinyGPU.h>
#include <TinyGPU/Boards/LCDBoards.h>
#include <TinyGPU/Integrations/LVGLDriver.h>
#include <lvgl.h>
#include <math.h>

// --- display geometry (Landscape) ----------------------------------------
constexpr int kDisplayWidth = 240;   // 320;  // Swapped for Landscape
constexpr int kDisplayHeight = 320;  // 240;

// Explicit buffer allocation size (320 * 20 lines * 2 bytes)
constexpr size_t kLvglBufferSize = kDisplayWidth * 2;

#ifdef ESP32
LCDBoardGuitionESP32_LVGL_2_4Display board;
#else
LCDBoardDesktopSDL board(kDisplayWidth, kDisplayHeight);
#endif
LVGLDriver<RGB565> lvglDriver(board.display(), kDisplayWidth, kDisplayHeight,
                              kLvglBufferSize);

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

// Slider drag: drives the panel's real backlight brightness via PWM on
// ESP32. Desktop has no backlight to drive, so it just updates the label.
void onSliderChanged(lv_event_t* e) {
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
  int32_t value = lv_slider_get_value(slider);
  lv_label_set_text_fmt(sliderLabel, "Backlight: %ld%%", static_cast<long>(value));
#ifdef ESP32
  ledcWrite(board.backlightPin(), map(value, 0, 100, 0, 255));
#endif
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

  if (!board.begin()) {
    Serial.println("Board initialization failed!");
    while (1);
  }

#ifdef ESP32
  // Reconfigure the board's backlight pin for PWM dimming - board.begin()
  // already drove it digitally HIGH (full brightness).
  ledcAttach(board.backlightPin(), 5000, 8);
  ledcWrite(board.backlightPin(), 255);
#endif

  lvglDriver.setTouchDriver(*board.touch());

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
