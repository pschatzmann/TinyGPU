#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string.h>

#include <vector>

#include "TinyGPU/Surface/ISurface.h"

namespace tinygpu {
namespace codec {

/// 8 bit per channel colour used while decoding (RGB565/RGB666/RGB888 targets are
/// converted from it).
struct Rgb8 {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  Rgb8() = default;
  Rgb8(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
};

/// Converts an 8 bit colour to the pixel type of a surface.
template <typename RGB_T>
RGB_T toColor(Rgb8 c) {
  return RGB_T(c.r, c.g, c.b);
}

/// Mixes c with the background bg; a is the opacity (255 = c unchanged).
inline Rgb8 composite(Rgb8 c, Rgb8 bg, uint8_t a) {
  if (a == 255) return c;
  const uint32_t ia = 255 - a;
  auto mix = [&](uint8_t fg, uint8_t back) -> uint8_t {
    return static_cast<uint8_t>((fg * a + back * ia + 127) / 255);
  };
  return Rgb8(mix(c.r, bg.r), mix(c.g, bg.g), mix(c.b, bg.b));
}

/**
 * @brief DEFLATE (RFC 1951) and zlib (RFC 1950) decoder for the PNG parser.
 *
 * The whole output is kept in memory, because back references may point to any
 * earlier byte. maxOut limits the output, so a corrupt stream cannot exhaust the
 * memory.
 */
class Inflate {
 public:
  /// Decompresses a zlib stream into out. Returns false if the stream is invalid.
  static bool zlib(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t maxOut) {
    if (inSize < 2) return false;
    const uint8_t cmf = in[0];
    const uint8_t flg = in[1];
    if ((cmf & 0x0F) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20) != 0) return false;
    Inflate state(in + 2, inSize - 2, out, maxOut);
    return state.blocks();
  }

 private:
  struct Huffman {
    uint16_t count[16];
    uint16_t symbol[320];
  };

  static constexpr int kMaxCodes = 320;

  Inflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t maxOut)
      : in_(in), inLen_(inSize), out_(out), maxOut_(maxOut) {}

  /// Reads n bits, least significant first. Returns -1 at the end of the input.
  int bits(int n) {
    while (bitcnt_ < n) {
      if (pos_ >= inLen_) return -1;
      bitbuf_ |= static_cast<uint32_t>(in_[pos_++]) << bitcnt_;
      bitcnt_ += 8;
    }
    const int value = static_cast<int>(bitbuf_ & ((1u << n) - 1));
    bitbuf_ >>= n;
    bitcnt_ -= n;
    return value;
  }

  /// Builds the canonical Huffman code for the given code lengths.
  static bool build(Huffman& h, const uint16_t* lengths, int n) {
    for (int len = 0; len < 16; ++len) h.count[len] = 0;
    for (int s = 0; s < n; ++s) h.count[lengths[s]]++;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
      left <<= 1;
      left -= h.count[len];
      if (left < 0) return false;  // over-subscribed
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
    for (int s = 0; s < n; ++s) {
      if (lengths[s] != 0) h.symbol[offs[lengths[s]]++] = static_cast<uint16_t>(s);
    }
    return true;
  }

  /// Decodes one symbol. Returns -1 on error.
  int decode(const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
      const int b = bits(1);
      if (b < 0) return -1;
      code |= b;
      const int count = h.count[len];
      if (code - count < first) return h.symbol[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  }

  bool codes(const Huffman& lit, const Huffman& dist) {
    static const uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                          31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                          2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,    25,
                                           33,   49,   65,   97,   129,  193,   257,   385,   513,   769,
                                           1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
    static const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
      int sym = decode(lit);
      if (sym < 0) return false;
      if (sym < 256) {
        if (out_.size() >= maxOut_) return false;
        out_.push_back(static_cast<uint8_t>(sym));
      } else if (sym == 256) {
        return true;  // end of block
      } else {
        sym -= 257;
        if (sym >= 29) return false;
        const int extra = bits(kLenExtra[sym]);
        if (extra < 0) return false;
        const size_t len = kLenBase[sym] + static_cast<size_t>(extra);
        const int ds = decode(dist);
        if (ds < 0 || ds >= 30) return false;
        const int dextra = bits(kDistExtra[ds]);
        if (dextra < 0) return false;
        const size_t d = kDistBase[ds] + static_cast<size_t>(dextra);
        if (d > out_.size() || out_.size() + len > maxOut_) return false;
        const size_t from = out_.size() - d;
        for (size_t i = 0; i < len; ++i) out_.push_back(out_[from + i]);
      }
    }
  }

  bool fixedTables() {
    uint16_t lengths[kMaxCodes];
    int s = 0;
    for (; s < 144; ++s) lengths[s] = 8;
    for (; s < 256; ++s) lengths[s] = 9;
    for (; s < 280; ++s) lengths[s] = 7;
    for (; s < 288; ++s) lengths[s] = 8;
    if (!build(lit_, lengths, 288)) return false;
    for (s = 0; s < 30; ++s) lengths[s] = 5;
    return build(dist_, lengths, 30);
  }

  bool dynamicTables() {
    static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = bits(5);
    const int ndist = bits(5);
    const int ncode = bits(4);
    if (nlen < 0 || ndist < 0 || ncode < 0) return false;
    const int numLit = nlen + 257;
    const int numDist = ndist + 1;
    if (numLit > 286 || numDist > 30) return false;

    uint16_t lengths[kMaxCodes] = {0};
    for (int i = 0; i < ncode + 4; ++i) {
      const int v = bits(3);
      if (v < 0) return false;
      lengths[kOrder[i]] = static_cast<uint16_t>(v);
    }
    Huffman lencode;
    if (!build(lencode, lengths, 19)) return false;

    int index = 0;
    for (int i = 0; i < kMaxCodes; ++i) lengths[i] = 0;
    while (index < numLit + numDist) {
      const int sym = decode(lencode);
      if (sym < 0) return false;
      if (sym < 16) {
        lengths[index++] = static_cast<uint16_t>(sym);
        continue;
      }
      uint16_t value = 0;
      int repeat = 0;
      if (sym == 16) {
        if (index == 0) return false;
        value = lengths[index - 1];
        repeat = bits(2);
        if (repeat < 0) return false;
        repeat += 3;
      } else if (sym == 17) {
        repeat = bits(3);
        if (repeat < 0) return false;
        repeat += 3;
      } else {
        repeat = bits(7);
        if (repeat < 0) return false;
        repeat += 11;
      }
      if (index + repeat > numLit + numDist) return false;
      while (repeat-- > 0) lengths[index++] = value;
    }
    if (lengths[256] == 0) return false;  // no end-of-block code
    if (!build(lit_, lengths, numLit)) return false;
    return build(dist_, lengths + numLit, numDist);
  }

  bool blocks() {
    int last = 0;
    do {
      last = bits(1);
      const int type = bits(2);
      if (last < 0 || type < 0) return false;
      if (type == 0) {  // stored block
        bitbuf_ = 0;
        bitcnt_ = 0;
        if (pos_ + 4 > inLen_) return false;
        const uint16_t len = static_cast<uint16_t>(in_[pos_] | (in_[pos_ + 1] << 8));
        const uint16_t nlen = static_cast<uint16_t>(in_[pos_ + 2] | (in_[pos_ + 3] << 8));
        pos_ += 4;
        if (static_cast<uint16_t>(len ^ 0xFFFF) != nlen) return false;
        if (pos_ + len > inLen_ || out_.size() + len > maxOut_) return false;
        out_.insert(out_.end(), in_ + pos_, in_ + pos_ + len);
        pos_ += len;
      } else if (type == 1) {
        if (!fixedTables() || !codes(lit_, dist_)) return false;
      } else if (type == 2) {
        if (!dynamicTables() || !codes(lit_, dist_)) return false;
      } else {
        return false;  // reserved block type
      }
    } while (last == 0);
    return true;
  }

  const uint8_t* in_;
  size_t inLen_;
  size_t pos_ = 0;
  uint32_t bitbuf_ = 0;
  int bitcnt_ = 0;
  std::vector<uint8_t>& out_;
  size_t maxOut_;
  Huffman lit_{};
  Huffman dist_{};
};

}  // namespace codec
}  // namespace tinygpu
