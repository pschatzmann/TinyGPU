#pragma once
#include <SDL.h>

#include <cstdint>
#include <vector>

#include "DisplayDriver.h"

namespace tinygpu {

/**
 * @brief Display class for rendering a Surface<RGB565> using the SDL2 library.
 * This class can be used to display the contents of a TinyGPU Surface<RGB565>
 * on a desktop environment for testing and debugging purposes.
 */
template <typename RGB_T = RGB565>
class DisplayDriverSDL : public DisplayDriver<RGB_T> {
 public:
  DisplayDriverSDL(size_t width, size_t height)
      : w_(width), h_(height) {}
  DisplayDriverSDL(const ISurface<RGB_T>& surface)
      : w_(surface.width()), h_(surface.height()) {}

  bool begin() override {
    SDL_Init(SDL_INIT_VIDEO);
    if (window_ == nullptr) {
      window_ = SDL_CreateWindow("RGB565 Display", SDL_WINDOWPOS_UNDEFINED,
                                 SDL_WINDOWPOS_UNDEFINED, w_, h_,
                                 SDL_WINDOW_SHOWN);
      if (window_ == nullptr) return false;
      renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
      if (renderer_ == nullptr) return false;
      texture_ =
          SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565,
                            SDL_TEXTUREACCESS_STREAMING, w_, h_);
      if (texture_ == nullptr) return false;
    }
    return true;
  }

 public:
  bool writeData(ISurface<RGB_T>& surface) override {
    setAddressWindow(0, 0, surface.width(), surface.height());
    if (surface.width() != w_ || surface.height() != h_) {
      return false;
    }
    SDL_Rect dstRect = {static_cast<int>(x_), static_cast<int>(y_),
                        static_cast<int>(w_), static_cast<int>(h_)};
    SDL_UpdateTexture(texture_, NULL, surface.data(), surface.size());
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, NULL, &dstRect);
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
  size_t x_ = 0;
  size_t y_ = 0;
  size_t w_ = 0;
  size_t h_ = 0;
  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;

 protected:
  bool setAddressWindow(size_t x, size_t y, size_t w, size_t h) override {
    x_ = x;
    y_ = y;
    w_ = w;
    h_ = h;
    return true;
  }
};

}  // namespace tinygpu