#pragma once
#include <SDL.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "TinyGPU/Drivers/DisplayDriver.h"
#include "TinyGPU/Color/RGB565.h"

namespace tinygpu {

/**
 * @brief Display class for rendering a Surface<RGB565> using the SDL2 library.
 * This class can be used to display the contents of a TinyGPU Surface<RGB565>
 * on a desktop environment for testing and debugging purposes.
 *
 * writeData() drains the SDL event queue on every call and exits the process
 * on SDL_QUIT (i.e. the window's close button), since sketches that never
 * touch a TouchDriverSDL - the only other place that pumps events - would
 * otherwise leave the window unresponsive to being closed: with no one
 * calling SDL_PollEvent, the window manager's close request just piles up in
 * SDL's queue and the OS reports the window as hung.
 *
 * The window can be larger than the display: setScale() (or the environment
 * variable TINYGPU_SDL_SCALE, which takes precedence) opens it with that
 * factor, e.g. 1.5 or 2, and it can be resized - the display is scaled to the
 * window (letterboxed if the aspect ratio differs): with sharp pixels for a
 * whole factor, smoothed otherwise. TouchDriverSDL maps the mouse back to
 * display coordinates.
 */
template <typename RGB_T = RGB565>
class DisplayDriverSDL : public DisplayDriver<RGB_T> {
 public:
  DisplayDriverSDL(size_t width, size_t height) : w_(width), h_(height) {}
  DisplayDriverSDL(const ISurface<RGB_T>& surface)
      : w_(surface.width()), h_(surface.height()) {}

  size_t width() const override { return w_; }
  size_t height() const override { return h_; }

  /// Size of the window as a multiple of the display, e.g. 1.5 or 2 (call
  /// before begin()).
  void setScale(float scale) { scale_ = scale < 1.0f ? 1.0f : scale; }
  float scale() const { return scale_; }

  /// Title of the window (call before begin()).
  void setTitle(const char* title) { title_ = title != nullptr ? title : ""; }

  bool begin() override {
    SDL_Init(SDL_INIT_VIDEO);
    if (window_ == nullptr) {
      const char* env = getenv("TINYGPU_SDL_SCALE");
      if (env != nullptr && atof(env) >= 1.0) scale_ = static_cast<float>(atof(env));
      // sharp pixels for a whole factor; smoothed otherwise (uneven pixels)
      const bool whole = scale_ == static_cast<float>(static_cast<int>(scale_));
      SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, whole ? "0" : "1");
      window_ = SDL_CreateWindow(title_, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 static_cast<int>(w_ * scale_ + 0.5f), static_cast<int>(h_ * scale_ + 0.5f),
                                 SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
      if (window_ == nullptr) return false;
      renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
      if (renderer_ == nullptr) return false;
      // draw in display pixels, scaled to the window
      SDL_RenderSetLogicalSize(renderer_, static_cast<int>(w_), static_cast<int>(h_));
      texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565,
                                   SDL_TEXTUREACCESS_STREAMING, w_, h_);
      if (texture_ == nullptr) return false;
    }
    return true;
  }

 public:
  bool writeData(ISurface<RGB_T>& surface) override {
    return writeData(surface, 0, 0);
  }

  bool writeData(ISurface<RGB_T>& surface, size_t x, size_t y) override {
    static_assert(sizeof(RGB_T) == 2,
                  "DisplayDriverSDL assumes a 16bpp RGB_T (RGB565)");
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        exit(0);
      }
    }
    setAddressWindow(x, y, surface.width(), surface.height());
    SDL_Rect updateRect = {static_cast<int>(x), static_cast<int>(y),
                           static_cast<int>(surface.width()),
                           static_cast<int>(surface.height())};
    // RGB_T (RGB565) values are stored byte-swapped to SPI/QSPI wire
    // order (see RGB565.h), but SDL_PIXELFORMAT_RGB565 expects the
    // conventional (native) bit layout - swap back into a scratch
    // buffer before uploading, rather than mutating the surface itself.
    const size_t n = surface.size();
    if (scratch_.size() < n) scratch_.resize(n);
    const uint16_t* src = reinterpret_cast<const uint16_t*>(surface.data());
    uint16_t* dst = reinterpret_cast<uint16_t*>(scratch_.data());
    const size_t pixelCount = n / sizeof(RGB_T);
    for (size_t i = 0; i < pixelCount; ++i) {
      dst[i] = RGB565::swapBytes(src[i]);
    }
    SDL_UpdateTexture(texture_, &updateRect, scratch_.data(),
                      surface.width() * sizeof(RGB_T));
    SDL_Rect fullRect = {0, 0, static_cast<int>(w_), static_cast<int>(h_)};
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, NULL, &fullRect);
    SDL_RenderPresent(renderer_);
    return true;
  }

  void end() override {
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_Quit();
    texture_ = nullptr;
    renderer_ = nullptr;
    window_ = nullptr;
  }

 protected:
  size_t w_ = 0;
  size_t h_ = 0;
  float scale_ = 1.0f;
  const char* title_ = "RGB565 Display";
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  std::vector<uint8_t> scratch_;

 protected:
  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    return true;
  }
};

}  // namespace tinygpu