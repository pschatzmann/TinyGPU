#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string.h>

#include <vector>

#include "TinyGPU/IO/ImageCodec.h"
#include "TinyGPU/Surface/ISurface.h"

namespace tinygpu {

/**
 * @brief PNG decoder for TinyGPU surfaces.
 *
 * Decodes a PNG file held in memory into an ISurface, which is resized to the
 * image size. All colour types (grayscale, RGB, palette, with or without alpha)
 * and bit depths 1, 2, 4, 8 and 16 are supported. Transparent pixels are mixed
 * with the background colour. Interlaced (Adam7) images are not supported.
 *
 * The decoded data is kept in memory while decoding (the decompressed image),
 * so the parser needs about (height * width * channels) bytes of RAM.
 *
 * @code
 * Surface<RGB565> image(1, 1, FontRGB565);
 * PNGParser<RGB565> parser(image);
 * if (parser.decode(data, size)) { ... drawSprite(..., image) ... }
 * @endcode
 */
template <typename RGB_T = RGB565>
class PNGParser {
 public:
  /// Creates a parser that decodes into target.
  explicit PNGParser(ISurface<RGB_T>& target) : target_(target) {}

  /// Decodes a PNG file. Returns false on error (see errorMessage()).
  bool decode(const uint8_t* data, size_t size, codec::Rgb8 background = codec::Rgb8(255, 255, 255)) {
    error_ = nullptr;
    width_ = 0;
    height_ = 0;
    if (data == nullptr || size < 8 || !hasSignature(data)) return fail("not a PNG file");

    Header hdr;
    std::vector<uint8_t> palette;  // RGB triples
    std::vector<uint8_t> alpha;    // palette transparency
    std::vector<uint8_t> idat;
    size_t pos = 8;
    bool haveHeader = false;
    bool haveEnd = false;
    while (pos + 12 <= size && !haveEnd) {
      const uint32_t len = be32(data + pos);
      const char* type = reinterpret_cast<const char*>(data + pos + 4);
      if (len > size - pos - 12) return fail("truncated chunk");
      const uint8_t* body = data + pos + 8;
      if (memcmp(type, "IHDR", 4) == 0) {
        if (len != 13) return fail("bad IHDR");
        hdr.width = be32(body);
        hdr.height = be32(body + 4);
        hdr.depth = body[8];
        hdr.colorType = body[9];
        hdr.interlace = body[12];
        if (body[10] != 0 || body[11] != 0) return fail("unknown compression or filter method");
        haveHeader = true;
      } else if (memcmp(type, "PLTE", 4) == 0) {
        if (len % 3 != 0 || len > 256 * 3) return fail("bad PLTE");
        palette.assign(body, body + len);
      } else if (memcmp(type, "tRNS", 4) == 0) {
        alpha.assign(body, body + len);
      } else if (memcmp(type, "IDAT", 4) == 0) {
        idat.insert(idat.end(), body, body + len);
      } else if (memcmp(type, "IEND", 4) == 0) {
        haveEnd = true;
      }
      pos += 12 + len;
    }
    if (!haveHeader) return fail("missing IHDR");
    if (idat.empty()) return fail("missing IDAT");
    if (hdr.width == 0 || hdr.height == 0) return fail("empty image");
    if (hdr.interlace != 0) return fail("interlaced PNG is not supported");

    const int channels = channelsOf(hdr.colorType);
    if (channels == 0 || !validDepth(hdr.colorType, hdr.depth)) return fail("unsupported colour type or depth");
    if (hdr.colorType == 3 && palette.empty()) return fail("palette missing");

    const size_t bitsPerPixel = static_cast<size_t>(channels) * hdr.depth;
    const size_t rowBytes = (static_cast<size_t>(hdr.width) * bitsPerPixel + 7) / 8;
    const size_t bpp = bitsPerPixel < 8 ? 1 : bitsPerPixel / 8;
    const size_t expected = static_cast<size_t>(hdr.height) * (rowBytes + 1);

    std::vector<uint8_t> raw;
    if (!codec::Inflate::zlib(idat.data(), idat.size(), raw, expected) || raw.size() < expected) {
      return fail("corrupt image data");
    }
    if (!target_.resize(hdr.width, hdr.height)) return fail("cannot resize the target");

    std::vector<uint8_t> prev(rowBytes, 0), cur(rowBytes, 0);
    for (uint32_t y = 0; y < hdr.height; ++y) {
      const uint8_t* line = raw.data() + y * (rowBytes + 1);
      if (!unfilter(line[0], line + 1, prev, cur, bpp)) return fail("bad filter type");
      for (uint32_t x = 0; x < hdr.width; ++x) {
        target_.setPixel(x, y, codec::toColor<RGB_T>(pixel(hdr, cur, x, palette, alpha, background)));
      }
      prev.swap(cur);
    }
    width_ = hdr.width;
    height_ = hdr.height;
    return true;
  }

  /// Returns the image width of the last decoded PNG (0 before a successful decode).
  size_t width() const { return width_; }
  /// Returns the image height of the last decoded PNG (0 before a successful decode).
  size_t height() const { return height_; }
  /// Returns the message of the last error, or nullptr.
  const char* errorMessage() const { return error_; }

  /// Reads the image size from the PNG header without decoding the pixels.
  static bool getSize(const uint8_t* data, size_t size, size_t& width, size_t& height) {
    if (data == nullptr || size < 24 || !hasSignature(data)) return false;
    if (memcmp(data + 12, "IHDR", 4) != 0) return false;
    width = be32(data + 16);
    height = be32(data + 20);
    return width > 0 && height > 0;
  }

 private:
  struct Header {
    uint32_t width = 0;
    uint32_t height = 0;
    uint8_t depth = 0;
    uint8_t colorType = 0;
    uint8_t interlace = 0;
  };

  static bool hasSignature(const uint8_t* p) {
    static const uint8_t kSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    return memcmp(p, kSignature, 8) == 0;
  }

  static uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
  }

  static int channelsOf(uint8_t colorType) {
    switch (colorType) {
      case 0: return 1;  // grayscale
      case 2: return 3;  // RGB
      case 3: return 1;  // palette index
      case 4: return 2;  // grayscale + alpha
      case 6: return 4;  // RGBA
      default: return 0;
    }
  }

  static bool validDepth(uint8_t colorType, uint8_t depth) {
    switch (colorType) {
      case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
      case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
      case 2:
      case 4:
      case 6: return depth == 8 || depth == 16;
      default: return false;
    }
  }

  static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    const int p = static_cast<int>(a) + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
  }

  /// Reverses the row filter of one scan line into out.
  static bool unfilter(uint8_t type, const uint8_t* in, const std::vector<uint8_t>& prev,
                       std::vector<uint8_t>& out, size_t bpp) {
    const size_t n = out.size();
    for (size_t i = 0; i < n; ++i) {
      const uint8_t left = i >= bpp ? out[i - bpp] : 0;
      const uint8_t up = prev[i];
      const uint8_t upLeft = i >= bpp ? prev[i - bpp] : 0;
      uint8_t predicted = 0;
      switch (type) {
        case 0: predicted = 0; break;
        case 1: predicted = left; break;
        case 2: predicted = up; break;
        case 3: predicted = static_cast<uint8_t>((left + up) / 2); break;
        case 4: predicted = paeth(left, up, upLeft); break;
        default: return false;
      }
      out[i] = static_cast<uint8_t>(in[i] + predicted);
    }
    return true;
  }

  /// The sample with the given index in a scan line, scaled to 0..255 unless it is a palette index.
  static uint8_t sample(const std::vector<uint8_t>& line, size_t index, uint8_t depth, bool scale) {
    if (depth == 16) return line[index * 2];
    if (depth == 8) return line[index];
    const size_t bit = index * depth;
    const uint8_t value = static_cast<uint8_t>((line[bit / 8] >> (8 - depth - bit % 8)) & ((1u << depth) - 1));
    if (!scale) return value;
    return static_cast<uint8_t>(value * 255 / ((1u << depth) - 1));
  }

  static codec::Rgb8 pixel(const Header& hdr, const std::vector<uint8_t>& line, uint32_t x,
                           const std::vector<uint8_t>& palette, const std::vector<uint8_t>& alpha,
                           codec::Rgb8 background) {
    const size_t channels = static_cast<size_t>(channelsOf(hdr.colorType));
    const size_t first = static_cast<size_t>(x) * channels;
    codec::Rgb8 c;
    uint8_t a = 255;
    switch (hdr.colorType) {
      case 0: {  // grayscale
        const uint8_t g = sample(line, first, hdr.depth, true);
        c = codec::Rgb8(g, g, g);
        break;
      }
      case 4: {  // grayscale + alpha
        const uint8_t g = sample(line, first, hdr.depth, true);
        c = codec::Rgb8(g, g, g);
        a = sample(line, first + 1, hdr.depth, true);
        break;
      }
      case 2:  // RGB
        c = codec::Rgb8(sample(line, first, hdr.depth, false), sample(line, first + 1, hdr.depth, false),
                        sample(line, first + 2, hdr.depth, false));
        break;
      case 6:  // RGBA
        c = codec::Rgb8(sample(line, first, hdr.depth, false), sample(line, first + 1, hdr.depth, false),
                        sample(line, first + 2, hdr.depth, false));
        a = sample(line, first + 3, hdr.depth, false);
        break;
      case 3: {  // palette
        const size_t index = sample(line, first, hdr.depth, false);
        if ((index + 1) * 3 <= palette.size()) {
          c = codec::Rgb8(palette[index * 3], palette[index * 3 + 1], palette[index * 3 + 2]);
        }
        if (index < alpha.size()) a = alpha[index];
        break;
      }
      default: break;
    }
    return codec::composite(c, background, a);
  }

  bool fail(const char* message) {
    error_ = message;
    return false;
  }

  ISurface<RGB_T>& target_;
  size_t width_ = 0;
  size_t height_ = 0;
  const char* error_ = nullptr;
};

}  // namespace tinygpu
