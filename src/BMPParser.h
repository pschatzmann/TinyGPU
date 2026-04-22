#pragma once

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <vector>

#include "ISurface.h"

namespace tinygpu {

/**
 * @brief Incremental BMP decoder for TinyGPU framebuffers.
 *
 * The parser accepts BMP file bytes through write(), buffers them until enough
 * data is available, then decodes supported uncompressed BMP formats directly
 * into the provided ISurface target using RGB565 pixels.
 */
template <typename RGB_T = RGB565>
class BMPParser {
public:
  /// Represents the parser status.
  enum class Status {
    Collecting,
    Complete,
    Error,
  };

  /// Creates a parser that decodes into the provided framebuffer target.
  BMPParser(ISurface<RGB_T>& target) : target_(target) {}

  /// Resets the parser to decode a new BMP image.
  void reset() {
    buffer_.clear();
    palette_.clear();
    status_ = Status::Collecting;
    errorMessage_ = nullptr;
    imageWidth_ = 0;
    imageHeight_ = 0;
    topDown_ = false;
  }

  /// Writes BMP data incrementally to the parser.
  size_t write(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0 || status_ != Status::Collecting) {
      return 0;
    }

    buffer_.insert(buffer_.end(), data, data + length);
    tryDecode();
    return length;
  }

  /// Writes BMP data incrementally to the parser.
  size_t write(uint8_t* data, size_t length) {
    return write(static_cast<const uint8_t*>(data), length);
  }

  /// Returns true when the full image has been decoded.
  bool isComplete() const { return status_ == Status::Complete; }

  /// Returns true when parsing failed.
  bool hasError() const { return status_ == Status::Error; }

  /// Returns the current parser status.
  Status status() const { return status_; }

  /// Returns the decoded image width in pixels.
  size_t width() const { return imageWidth_; }

  /// Returns the decoded image height in pixels.
  size_t height() const { return imageHeight_; }

  /// Returns the latest error message, if any.
  const char* errorMessage() const { return errorMessage_; }

 protected:
  /// Stores parsed BMP metadata.
  struct HeaderInfo {
    uint32_t fileSize = 0;
    uint32_t pixelOffset = 0;
    uint32_t dibHeaderSize = 0;
    int32_t width = 0;
    int32_t height = 0;
    uint16_t planes = 0;
    uint16_t bitsPerPixel = 0;
    uint32_t compression = 0;
    uint32_t imageSize = 0;
    uint32_t colorsUsed = 0;
    uint32_t redMask = 0;
    uint32_t greenMask = 0;
    uint32_t blueMask = 0;
    uint32_t alphaMask = 0;
  };

  ISurface<RGB_T>& target_;
  std::vector<uint8_t> buffer_;
  std::vector<RGB_T> palette_;
  Status status_ = Status::Collecting;
  const char* errorMessage_ = nullptr;
  size_t imageWidth_ = 0;
  size_t imageHeight_ = 0;
  bool topDown_ = false;

  static constexpr uint32_t kCompressionRgb = 0;
  static constexpr uint32_t kCompressionBitfields = 3;

  void tryDecode() {
    if (status_ != Status::Collecting) {
      return;
    }
    if (buffer_.size() < 26) {
      return;
    }

    HeaderInfo header;
    if (!parseHeader(header)) {
      return;
    }

    const uint32_t requiredSize = requiredDataSize(header);
    if (requiredSize == 0 || buffer_.size() < requiredSize) {
      return;
    }

    if (!decode(header)) {
      return;
    }

    status_ = Status::Complete;
  }

  bool parseHeader(HeaderInfo& header) {
    if (buffer_.size() < 54) {
      return false;
    }

    if (readU16(0) != 0x4D42) {
      setError("Invalid BMP signature");
      return false;
    }

    header.fileSize = readU32(2);
    header.pixelOffset = readU32(10);
    header.dibHeaderSize = readU32(14);
    if (header.dibHeaderSize < 40) {
      setError("Unsupported BMP DIB header");
      return false;
    }

    if (buffer_.size() < 14 + header.dibHeaderSize) {
      return false;
    }

    header.width = readS32(18);
    header.height = readS32(22);
    header.planes = readU16(26);
    header.bitsPerPixel = readU16(28);
    header.compression = readU32(30);
    header.imageSize = readU32(34);
    header.colorsUsed = readU32(46);

    if (header.planes != 1) {
      setError("Unsupported BMP plane count");
      return false;
    }
    if (header.width <= 0 || header.height == 0) {
      setError("Invalid BMP dimensions");
      return false;
    }

    if (header.bitsPerPixel != 8 && header.bitsPerPixel != 16 &&
        header.bitsPerPixel != 24 && header.bitsPerPixel != 32) {
      setError("Unsupported BMP bit depth");
      return false;
    }

    if (header.compression != kCompressionRgb &&
        header.compression != kCompressionBitfields) {
      setError("Unsupported BMP compression");
      return false;
    }

    topDown_ = header.height < 0;
    imageWidth_ = static_cast<size_t>(header.width);
    imageHeight_ =
        static_cast<size_t>(topDown_ ? -header.height : header.height);

    if (!parseBitMasks(header)) {
      return false;
    }
    return parsePalette(header);
  }

  bool parseBitMasks(HeaderInfo& header) {
    if (header.bitsPerPixel == 16 || header.bitsPerPixel == 32) {
      if (header.compression == kCompressionBitfields) {
        const size_t maskOffset =
            14 + (header.dibHeaderSize >= 52 ? 40 : header.dibHeaderSize);
        const size_t requiredMasks = header.dibHeaderSize >= 56 ? 16 : 12;
        if (buffer_.size() < maskOffset + requiredMasks) {
          return false;
        }

        header.redMask = readU32(maskOffset);
        header.greenMask = readU32(maskOffset + 4);
        header.blueMask = readU32(maskOffset + 8);
        if (requiredMasks == 16) {
          header.alphaMask = readU32(maskOffset + 12);
        }
      } else if (header.bitsPerPixel == 16) {
        header.redMask = 0x7C00;
        header.greenMask = 0x03E0;
        header.blueMask = 0x001F;
      } else {
        header.redMask = 0x00FF0000;
        header.greenMask = 0x0000FF00;
        header.blueMask = 0x000000FF;
      }
    }

    return true;
  }

  bool parsePalette(const HeaderInfo& header) {
    palette_.clear();
    if (header.bitsPerPixel != 8) {
      return true;
    }

    const uint32_t paletteEntryCount =
        header.colorsUsed != 0 ? header.colorsUsed : 256;
    const size_t paletteOffset = 14 + header.dibHeaderSize;
    const size_t paletteSize = static_cast<size_t>(paletteEntryCount) * 4;
    if (buffer_.size() < paletteOffset + paletteSize) {
      return false;
    }

    palette_.reserve(paletteEntryCount);
    for (uint32_t index = 0; index < paletteEntryCount; ++index) {
      const size_t entryOffset =
          paletteOffset + (static_cast<size_t>(index) * 4);
      palette_.push_back(RGB565(buffer_[entryOffset + 2],
                                buffer_[entryOffset + 1],
                                buffer_[entryOffset]));
    }
    return true;
  }

  uint32_t requiredDataSize(const HeaderInfo& header) const {
    const uint32_t rowStride = bmpRowStride(header.bitsPerPixel, imageWidth_);
    const uint32_t computedImageSize =
        rowStride * static_cast<uint32_t>(imageHeight_);
    const uint32_t pixelBytes =
        header.imageSize != 0 ? header.imageSize : computedImageSize;
    const uint32_t computedSize = header.pixelOffset + pixelBytes;

    if (header.fileSize != 0) {
      return std::max(header.fileSize, computedSize);
    }
    return computedSize;
  }

  bool decode(const HeaderInfo& header) {
    if (header.pixelOffset >= buffer_.size()) {
      setError("Invalid BMP pixel offset");
      return false;
    }

    target_.resize(imageWidth_, imageHeight_);
    const uint32_t rowStride = bmpRowStride(header.bitsPerPixel, imageWidth_);
    for (size_t row = 0; row < imageHeight_; ++row) {
      const size_t sourceRow = topDown_ ? row : (imageHeight_ - 1 - row);
      const size_t rowOffset = header.pixelOffset + (sourceRow * rowStride);
      if (rowOffset + rowStride > buffer_.size()) {
        setError("Incomplete BMP pixel data");
        return false;
      }

      if (!decodeRow(header, rowOffset, row)) {
        return false;
      }
    }

    return true;
  }

  bool decodeRow(const HeaderInfo& header, size_t rowOffset, size_t targetY) {
    switch (header.bitsPerPixel) {
      case 8:
        return decode8BitRow(rowOffset, targetY);
      case 16:
        return decode16BitRow(header, rowOffset, targetY);
      case 24:
        return decode24BitRow(rowOffset, targetY);
      case 32:
        return decode32BitRow(header, rowOffset, targetY);
      default:
        setError("Unsupported BMP row format");
        return false;
    }
  }

  bool decode8BitRow(size_t rowOffset, size_t targetY) {
    if (palette_.empty()) {
      setError("Missing BMP palette");
      return false;
    }

    for (size_t x = 0; x < imageWidth_; ++x) {
      const uint8_t paletteIndex = buffer_[rowOffset + x];
      if (paletteIndex >= palette_.size()) {
        setError("BMP palette index out of range");
        return false;
      }
      target_.setPixel(x, targetY, palette_[paletteIndex]);
    }
    return true;
  }

  bool decode16BitRow(const HeaderInfo& header, size_t rowOffset,
                      size_t targetY) {
    for (size_t x = 0; x < imageWidth_; ++x) {
      const uint16_t pixelValue =
          static_cast<uint16_t>(buffer_[rowOffset + (x * 2)]) |
          (static_cast<uint16_t>(buffer_[rowOffset + (x * 2) + 1]) << 8);
      target_.setPixel(x, targetY, decodeMaskedPixel(pixelValue, header));
    }
    return true;
  }

  bool decode24BitRow(size_t rowOffset, size_t targetY) {
    for (size_t x = 0; x < imageWidth_; ++x) {
      const size_t pixelOffset = rowOffset + (x * 3);
      target_.setPixel(x, targetY,
                       RGB565(buffer_[pixelOffset + 2],
                              buffer_[pixelOffset + 1], buffer_[pixelOffset]));
    }
    return true;
  }

  bool decode32BitRow(const HeaderInfo& header, size_t rowOffset,
                      size_t targetY) {
    for (size_t x = 0; x < imageWidth_; ++x) {
      const size_t pixelOffset = rowOffset + (x * 4);
      const uint32_t pixelValue =
          static_cast<uint32_t>(buffer_[pixelOffset]) |
          (static_cast<uint32_t>(buffer_[pixelOffset + 1]) << 8) |
          (static_cast<uint32_t>(buffer_[pixelOffset + 2]) << 16) |
          (static_cast<uint32_t>(buffer_[pixelOffset + 3]) << 24);
      target_.setPixel(x, targetY, decodeMaskedPixel(pixelValue, header));
    }
    return true;
  }

  RGB565 decodeMaskedPixel(uint32_t pixelValue,
                           const HeaderInfo& header) const {
    const uint8_t red = extractChannel(pixelValue, header.redMask);
    const uint8_t green = extractChannel(pixelValue, header.greenMask);
    const uint8_t blue = extractChannel(pixelValue, header.blueMask);
    return RGB565(red, green, blue);
  }

  static uint8_t extractChannel(uint32_t pixelValue, uint32_t mask) {
    if (mask == 0) {
      return 0;
    }

    uint8_t shift = 0;
    uint8_t bits = 0;
    while (((mask >> shift) & 0x1U) == 0U) {
      ++shift;
    }
    while (((mask >> (shift + bits)) & 0x1U) != 0U) {
      ++bits;
    }

    const uint32_t value = (pixelValue & mask) >> shift;
    const uint32_t maxValue = (1UL << bits) - 1UL;
    return maxValue == 0 ? 0 : static_cast<uint8_t>((value * 255UL) / maxValue);
  }

  static uint32_t bmpRowStride(uint16_t bitsPerPixel, size_t width) {
    const uint32_t bitsPerRow = static_cast<uint32_t>(width) * bitsPerPixel;
    return ((bitsPerRow + 31U) / 32U) * 4U;
  }

  uint16_t readU16(size_t offset) const {
    return static_cast<uint16_t>(buffer_[offset]) |
           (static_cast<uint16_t>(buffer_[offset + 1]) << 8);
  }

  uint32_t readU32(size_t offset) const {
    return static_cast<uint32_t>(buffer_[offset]) |
           (static_cast<uint32_t>(buffer_[offset + 1]) << 8) |
           (static_cast<uint32_t>(buffer_[offset + 2]) << 16) |
           (static_cast<uint32_t>(buffer_[offset + 3]) << 24);
  }

  int32_t readS32(size_t offset) const {
    return static_cast<int32_t>(readU32(offset));
  }

  void setError(const char* message) {
    status_ = Status::Error;
    errorMessage_ = message;
  }
};

}  // namespace tinygpu