#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string.h>

#include <vector>

#include "TinyGPU/IO/ImageCodec.h"
#include "TinyGPU/Surface/ISurface.h"

namespace tinygpu {

/**
 * @brief GIF decoder for TinyGPU surfaces (GIF87a and GIF89a).
 *
 * Decodes the first image of a GIF file held in memory into an ISurface. The
 * surface is resized to the logical screen of the GIF. The area outside the
 * first image, and its transparent pixels, get the background colour. Local and
 * global palettes and interlaced images are supported. Animations show their
 * first frame only.
 *
 * The compressed data of the image is kept in memory while decoding.
 *
 * @code
 * Surface<RGB565> image(1, 1, FontRGB565);
 * GIFParser<RGB565> parser(image);
 * if (parser.decode(data, size)) { ... }
 * @endcode
 */
template <typename RGB_T = RGB565>
class GIFParser {
 public:
  /// Creates a parser that decodes into target.
  explicit GIFParser(ISurface<RGB_T>& target) : target_(target) {}

  /// Decodes the first image of a GIF file. Returns false on error (see errorMessage()).
  bool decode(const uint8_t* data, size_t size, codec::Rgb8 background = codec::Rgb8(255, 255, 255)) {
    error_ = nullptr;
    width_ = 0;
    height_ = 0;
    if (data == nullptr || size < 13 || memcmp(data, "GIF8", 4) != 0) return fail("not a GIF file");
    if (data[4] != '7' && data[4] != '9') return fail("unknown GIF version");
    if (data[5] != 'a') return fail("unknown GIF version");

    const uint32_t screenW = le16(data + 6);
    const uint32_t screenH = le16(data + 8);
    if (screenW == 0 || screenH == 0) return fail("empty image");
    const uint8_t screenFlags = data[10];
    size_t pos = 13;
    std::vector<uint8_t> globalPalette;
    if (screenFlags & 0x80) {
      const size_t n = 3u * (size_t(1) << ((screenFlags & 7) + 1));
      if (pos + n > size) return fail("truncated palette");
      globalPalette.assign(data + pos, data + pos + n);
      pos += n;
    }

    bool transparent = false;
    uint8_t transparentIndex = 0;
    while (pos < size) {
      const uint8_t block = data[pos++];
      if (block == 0x21) {  // extension
        if (pos >= size) break;
        const uint8_t label = data[pos++];
        if (label == 0xF9 && pos + 6 <= size && data[pos] >= 4) {  // graphic control
          transparent = (data[pos + 1] & 1) != 0;
          transparentIndex = data[pos + 4];
        }
        if (!skipSubBlocks(data, size, pos)) return fail("truncated extension");
      } else if (block == 0x2C) {  // image descriptor
        if (pos + 9 > size) return fail("truncated image descriptor");
        const uint32_t left = le16(data + pos);
        const uint32_t top = le16(data + pos + 2);
        const uint32_t w = le16(data + pos + 4);
        const uint32_t h = le16(data + pos + 6);
        const uint8_t flags = data[pos + 8];
        pos += 9;
        std::vector<uint8_t> palette = globalPalette;
        if (flags & 0x80) {
          const size_t n = 3u * (size_t(1) << ((flags & 7) + 1));
          if (pos + n > size) return fail("truncated palette");
          palette.assign(data + pos, data + pos + n);
          pos += n;
        }
        if (palette.empty()) return fail("palette missing");
        if (pos >= size) return fail("truncated image");
        const int minCode = data[pos++];
        if (minCode < 2 || minCode > 8) return fail("bad LZW code size");
        std::vector<uint8_t> compressed;
        if (!collectSubBlocks(data, size, pos, compressed)) return fail("truncated image data");
        std::vector<uint8_t> indices;
        if (!lzw(compressed, minCode, indices, static_cast<size_t>(w) * h)) return fail("corrupt image data");
        if (indices.size() < static_cast<size_t>(w) * h) return fail("missing image data");

        if (!target_.resize(screenW, screenH)) return fail("cannot resize the target");
        const RGB_T backgroundColor = codec::toColor<RGB_T>(background);
        target_.clear(backgroundColor);
        const std::vector<uint32_t> rows = rowOrder(h, (flags & 0x40) != 0);
        for (uint32_t k = 0; k < h; ++k) {
          const uint32_t y = top + rows[k];
          if (y >= screenH) continue;
          for (uint32_t x = 0; x < w; ++x) {
            const uint32_t sx = left + x;
            if (sx >= screenW) break;
            const uint8_t index = indices[static_cast<size_t>(k) * w + x];
            if (transparent && index == transparentIndex) continue;
            codec::Rgb8 c;
            if (static_cast<size_t>(index) * 3 + 2 < palette.size()) {
              c = codec::Rgb8(palette[index * 3], palette[index * 3 + 1], palette[index * 3 + 2]);
            }
            target_.setPixel(sx, y, codec::toColor<RGB_T>(c));
          }
        }
        width_ = screenW;
        height_ = screenH;
        return true;
      } else {
        break;  // trailer (0x3B) or unknown data: no image
      }
    }
    return fail("no image in the GIF file");
  }

  /// Returns the logical screen width of the last decoded GIF (0 before a successful decode).
  size_t width() const { return width_; }
  /// Returns the logical screen height of the last decoded GIF (0 before a successful decode).
  size_t height() const { return height_; }
  /// Returns the message of the last error, or nullptr.
  const char* errorMessage() const { return error_; }

  /// Reads the logical screen size from the GIF header without decoding the image.
  static bool getSize(const uint8_t* data, size_t size, size_t& width, size_t& height) {
    if (data == nullptr || size < 10 || memcmp(data, "GIF8", 4) != 0) return false;
    width = le16(data + 6);
    height = le16(data + 8);
    return width > 0 && height > 0;
  }

 private:
  static uint32_t le16(const uint8_t* p) { return static_cast<uint32_t>(p[0] | (p[1] << 8)); }

  /// Skips the data sub-blocks (size byte, data, ..., 0 terminator).
  static bool skipSubBlocks(const uint8_t* data, size_t size, size_t& pos) {
    while (pos < size) {
      const uint8_t n = data[pos++];
      if (n == 0) return true;
      if (pos + n > size) return false;
      pos += n;
    }
    return false;
  }

  /// Copies the data sub-blocks into out.
  static bool collectSubBlocks(const uint8_t* data, size_t size, size_t& pos, std::vector<uint8_t>& out) {
    while (pos < size) {
      const uint8_t n = data[pos++];
      if (n == 0) return true;
      if (pos + n > size) return false;
      out.insert(out.end(), data + pos, data + pos + n);
      pos += n;
    }
    return false;
  }

  /// The image row that the k-th stored row belongs to (interlaced images are stored in four passes).
  static std::vector<uint32_t> rowOrder(uint32_t h, bool interlaced) {
    std::vector<uint32_t> rows;
    if (!interlaced) {
      for (uint32_t y = 0; y < h; ++y) rows.push_back(y);
      return rows;
    }
    static const uint8_t kStart[4] = {0, 4, 2, 1};
    static const uint8_t kStep[4] = {8, 8, 4, 2};
    for (int pass = 0; pass < 4; ++pass) {
      for (uint32_t y = kStart[pass]; y < h; y += kStep[pass]) rows.push_back(y);
    }
    return rows;
  }

  /// LZW decoder for GIF images (variable code size, clear and end codes).
  static bool lzw(const std::vector<uint8_t>& data, int minCode, std::vector<uint8_t>& out, size_t expected) {
    const int clear = 1 << minCode;
    const int end = clear + 1;
    int codeSize = minCode + 1;
    int next = end + 1;
    std::vector<uint16_t> prefix(4096, 0);
    std::vector<uint8_t> suffix(4096, 0);
    uint8_t stack[4097];
    int prev = -1;
    uint32_t acc = 0;
    int accBits = 0;
    size_t p = 0;
    out.clear();
    out.reserve(expected);

    for (;;) {
      while (accBits < codeSize) {
        if (p >= data.size()) return out.size() >= expected;
        acc |= static_cast<uint32_t>(data[p++]) << accBits;
        accBits += 8;
      }
      const int code = static_cast<int>(acc & ((1u << codeSize) - 1));
      acc >>= codeSize;
      accBits -= codeSize;

      if (code == clear) {
        codeSize = minCode + 1;
        next = end + 1;
        prev = -1;
        continue;
      }
      if (code == end) break;
      if (prev < 0) {  // first code after a clear: must be a literal
        if (code >= clear) return false;
        out.push_back(static_cast<uint8_t>(code));
        prev = code;
        continue;
      }
      if (code > next) return false;

      // the string of prev (plus its first byte when code is not yet in the table)
      const int source = code < next ? code : prev;
      auto firstByte = [&](int c) {
        while (c >= clear) c = prefix[c];
        return static_cast<uint8_t>(c);
      };
      const uint8_t first = firstByte(source);
      int n = 0;
      for (int c = source; c >= clear; c = prefix[c]) stack[n++] = suffix[c];
      stack[n++] = firstByte(source);  // the literal at the root of the chain
      for (int i = n - 1; i >= 0; --i) out.push_back(stack[i]);
      if (code == next) out.push_back(first);  // KwKwK case

      if (next < 4096) {
        prefix[next] = static_cast<uint16_t>(prev);
        suffix[next] = code < next ? firstByte(code) : first;
        ++next;
        if (next == (1 << codeSize) && codeSize < 12) ++codeSize;
      }
      prev = code;
      if (out.size() >= expected) break;
    }
    return out.size() >= expected;
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
