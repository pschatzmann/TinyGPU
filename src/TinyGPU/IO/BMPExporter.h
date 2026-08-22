#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ISurface.h"
#include "Print.h"

namespace tinygpu {

/**
 * @brief Writes TinyGPU surfaces as uncompressed 24-bit BMP data.
 *
 * The exporter serializes any ISurface-compatible surface to a standard BMP
 * stream using bottom-up BGR pixel rows padded to 4-byte boundaries.
 */
template <typename RGB_T = RGB565>
class BMPExporter {
public:
  BMPExporter(const ISurface<RGB_T>& surface) : surface_(surface) {}

  /// Writes the target surface as BMP data to the provided Print output.
  bool save(Print& out) const {
    const size_t width = surface_.width();
    const size_t height = surface_.height();
    if (width == 0 || height == 0) {
      return false;
    }

    const uint32_t rowStride = bmpRowStride(width);
    const uint32_t imageSize = rowStride * static_cast<uint32_t>(height);
    const uint32_t pixelOffset = 54;
    const uint32_t fileSize = pixelOffset + imageSize;

    if (!writeFileHeader(out, fileSize, pixelOffset) ||
        !writeInfoHeader(out, width, height, imageSize)) {
      return false;
    }

    const uint8_t padding[3] = {0, 0, 0};
    const size_t paddingSize = rowStride - (width * 3U);

    for (size_t row = height; row > 0; --row) {
      const size_t y = row - 1;
      for (size_t x = 0; x < width; ++x) {
        const RGB_T color = surface_.getPixel(x, y);
        const uint8_t pixel[3] = {color.getBlue(), color.getGreen(),
                                  color.getRed()};
        if (!writeBytes(out, pixel, sizeof(pixel))) {
          return false;
        }
      }

      if (paddingSize != 0 && !writeBytes(out, padding, paddingSize)) {
        return false;
      }
    }

    return true;
  }

 protected:
  const ISurface<RGB_T>& surface_;

  static uint32_t bmpRowStride(size_t width) {
    return static_cast<uint32_t>(((width * 3U) + 3U) & ~0x3U);
  }

  static bool writeFileHeader(Print& out, uint32_t fileSize,
                              uint32_t pixelOffset) {
    return writeU16(out, 0x4D42) && writeU32(out, fileSize) &&
           writeU16(out, 0) && writeU16(out, 0) && writeU32(out, pixelOffset);
  }

  static bool writeInfoHeader(Print& out, size_t width, size_t height,
                              uint32_t imageSize) {
    return writeU32(out, 40) && writeU32(out, static_cast<uint32_t>(width)) &&
           writeU32(out, static_cast<uint32_t>(height)) && writeU16(out, 1) &&
           writeU16(out, 24) && writeU32(out, 0) && writeU32(out, imageSize) &&
           writeU32(out, 2835) && writeU32(out, 2835) && writeU32(out, 0) &&
           writeU32(out, 0);
  }

  static bool writeU16(Print& out, uint16_t value) {
    const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xFFU),
                              static_cast<uint8_t>((value >> 8) & 0xFFU)};
    return writeBytes(out, bytes, sizeof(bytes));
  }

  static bool writeU32(Print& out, uint32_t value) {
    const uint8_t bytes[4] = {static_cast<uint8_t>(value & 0xFFU),
                              static_cast<uint8_t>((value >> 8) & 0xFFU),
                              static_cast<uint8_t>((value >> 16) & 0xFFU),
                              static_cast<uint8_t>((value >> 24) & 0xFFU)};
    return writeBytes(out, bytes, sizeof(bytes));
  }

  static bool writeBytes(Print& out, const uint8_t* data, size_t length) {
    return length == 0 || out.write(data, length) == length;
  }
};

}  // namespace tinygpu