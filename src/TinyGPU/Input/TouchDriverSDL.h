#pragma once
#include <SDL.h>

#include <cstdint>
#include <cstdlib>

#include "TinyGPU/Input/TouchDriverCommon.h"

namespace tinygpu {

/**
 * @brief Maps the desktop mouse to a TouchDriver, for exercising touch-driven
 * UI (buttons, sliders, ...) on the SDL desktop backend without any physical
 * touch hardware.
 *
 * The left mouse button stands in for a finger: button down = touched,
 * cursor position = touch point. SDL_QUIT (closing the window) exits the
 * process, since there is no hardware equivalent to hand back to the sketch.
 *
 * Like every TouchDriver, isTouched() and getPoint() do not implicitly loop
 * or block. isTouched() is where the SDL event queue is drained (via
 * SDL_PollEvent), so - as with the other drivers in this file - call it
 * exactly once per loop() iteration.
 */
class TouchDriverSDL : public TouchDriver {
 public:
  TouchDriverSDL() = default;

  bool begin() override {
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    return true;
  }

  bool isTouched() override {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        exit(0);
      }
    }

    int x = 0;
    int y = 0;
    const uint32_t buttons = SDL_GetMouseState(&x, &y);
    lastX_ = static_cast<int16_t>(x);
    lastY_ = static_cast<int16_t>(y);
    down_ = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    return down_;
  }

  bool getPoint(Point& outPoint) override {
    if (!down_) return false;
    outPoint = mapCoordinates(lastX_, lastY_, 255);
    return true;
  }

 private:
  int16_t lastX_ = 0;
  int16_t lastY_ = 0;
  bool down_ = false;
};

}  // namespace tinygpu
