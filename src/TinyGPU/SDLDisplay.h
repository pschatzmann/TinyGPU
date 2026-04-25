#pragma once
#include <SDL.h>

#include <cstdint>
#include <vector>

namespace tinygpu {

/**
 * @brief Display class for rendering a Surface<RGB565> using the SDL2 library.
 * This class can be used to display the contents of a TinyGPU Surface<RGB565>
 * on a desktop environment for testing and debugging purposes.
 */
class SDLDisplay {
  /// Initializes SDL and creates a window and texture for rendering. Must be
  /// called before display().
  bool begin(int width, int height) {
    this->width = width;
    this->height = height;

    SDL_Init(SDL_INIT_VIDEO);
    // create an SDL window
    window = SDL_CreateWindow("RGB565 Display", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, width, height,
                              SDL_WINDOW_SHOWN);
    if (window == nullptr) return false;

    // create an accelerated renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) return false;

    // Create an SDL texture in RGB565 format
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
    if (texture == nullptr) return false;

    return true;
  }

  /// begin() that takes an ISurface and uses its dimensions
  bool begin(ISurface& surface) {
    return begin(surface.width(), surface.height());
  }

  /// Renders the given SurfaceRGB565 to the SDL window.
  bool display(SurfaceRGB565& surface) {
    if (surface.width() != width || surface.height() != height) {
      TinyGPULogger.log(
          TinyGPULogger::ERROR,
          "Surface size (%zu x %zu) does not match display size (%d x %d)",
          surface.width(), surface.height(), width, height);
      return false;
    }

    SDL_UpdateTexture(texture, NULL, surface.data(), surface.size());
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
  }

  /// Clean up SDL resources
  void end() {
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    texture = nullptr;
    renderer = nullptr;
    window = nullptr;
  }

 protected:
  const int width = 0;
  const int height = 0;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_Texture* texture = nullptr;

};

}  // namespace tinygpu
