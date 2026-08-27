#pragma once

#include <stddef.h>
#include <stdint.h>

#include <TinyJPEGDecoder.h>  // https://github.com/pschatzmann/TinyJPEG

#include "TinyGPUConfig.h"
#include "TinyGPU/Surface/ISurface.h"

namespace tinygpu {

/**
 * @brief Decodes baseline JPEG images into a TinyGPU ISurface, using
 * pschatzmann/TinyJPEG (https://github.com/pschatzmann/TinyJPEG) as the
 * underlying decoder.
 *
 * TinyJPEG is a separate, optional Arduino/CMake library - install it
 * alongside TinyGPU and `#include <TinyGPU/IO/JPEGParser.h>` explicitly
 * (this header is not pulled in by TinyGPU.h) to opt in to JPEG support.
 *
 * TinyJPEG streams decoded pixels one MCU block at a time rather than
 * requiring memory for the whole decoded frame, so decode() resizes the
 * target surface once (from the JPEG's own header) and then just fans
 * each block out to setPixel() as it arrives - no intermediate frame
 * buffer beyond the target surface itself.
 */
template <typename RGB_T = RGB565>
class JPEGParser {
 public:
  explicit JPEGParser(ISurface<RGB_T>& target) : target_(target) {
    // TinyJPEG's default output is packed RGB565 in conventional (native)
    // bit order; RGB565::RGB565(uint16_t) expects the byte-swapped wire
    // order this library stores instead (see RGB565.h) - setSwapBytes(true)
    // makes the decoder do that swap for us so onBlock() can construct
    // RGB_T directly from each output pixel.
    decoder_.setSwapBytes(true);
    decoder_.setCallback(&JPEGParser::onBlock);
  }

  /// Sets the output reduction factor: 1 (full size), 2, 4, or 8.
  void setScale(uint8_t scale) { decoder_.setJpgScale(scale); }

  /// Decodes a JPEG held entirely in memory into the target surface at (0, 0).
  /// Resizes the target surface to the JPEG's own dimensions first.
  bool decode(const uint8_t* data, size_t size) {
    uint16_t w = 0, h = 0;
    if (decoder_.getJpgSize(&w, &h, data, size) != JDR_OK) {
      return setError("Could not read JPEG header");
    }
    if (!target_.resize(w, h)) {
      return setError("Could not resize target surface");
    }
    decoder_.setUserData(this);
    return checkResult(decoder_.drawJpg(0, 0, data, size));
  }

  /// Decodes a JPEG read from any file-like object (Arduino's fs::File,
  /// SD's File, or anything else exposing available()/read()/position()/
  /// seek() - see TinyJPEG's docs/decoding.md) into the target surface at
  /// (0, 0). Resizes the target surface to the JPEG's own dimensions
  /// first. `file` is read from its current position and left wherever
  /// the decode stopped; this method rewinds to that starting position
  /// only between reading the header and decoding the pixels.
  template <typename FileT>
  bool decode(FileT& file) {
    const size_t startPos = file.position();
    uint16_t w = 0, h = 0;
    if (decoder_.getJpgSize(&w, &h, file) != JDR_OK) {
      return setError("Could not read JPEG header");
    }
    if (!target_.resize(w, h)) {
      return setError("Could not resize target surface");
    }
    file.seek(startPos);
    decoder_.setUserData(this);
    return checkResult(decoder_.drawJpg(0, 0, file));
  }

  /// Returns the latest error message, if any.
  const char* errorMessage() const { return errorMessage_; }

 protected:
  ISurface<RGB_T>& target_;
  tinyjpeg::TinyJPEGDecoder decoder_;
  const char* errorMessage_ = nullptr;

  bool setError(const char* message) {
    errorMessage_ = message;
    return false;
  }

  bool checkResult(JRESULT result) {
    if (result == JDR_OK) {
      errorMessage_ = nullptr;
      return true;
    }
    return setError("JPEG decode failed");
  }

  static bool onBlock(tinyjpeg::TinyJPEGDecoder& decoder, int16_t x, int16_t y,
                      uint16_t w, uint16_t h, uint16_t* data) {
    auto* self = static_cast<JPEGParser*>(decoder.getUserData());
    for (uint16_t row = 0; row < h; ++row) {
      const size_t py = static_cast<size_t>(y) + row;
      for (uint16_t col = 0; col < w; ++col) {
        const size_t px = static_cast<size_t>(x) + col;
        // Trailing MCU blocks commonly overhang the image's right/bottom
        // edge (JPEG blocks are 8x8/16x16-aligned; the image itself
        // usually isn't) - drop whatever falls outside the target.
        if (self->target_.contains(px, py)) {
          self->target_.setPixel(px, py, RGB_T(data[(row * w) + col]));
        }
      }
    }
    return true;
  }
};

}  // namespace tinygpu
