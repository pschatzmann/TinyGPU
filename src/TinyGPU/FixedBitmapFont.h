#pragma once

#include <stddef.h>
#include <stdint.h>

#include <array>

#include "IFont.h"
#include "ISurface.h"
#include "RGB565.h"

namespace tinygpu {

/**
 * @brief Fixed-size bitmap font backed by a caller-supplied glyph table.
 *
 * @tparam Width Glyph width in pixels.
 * @tparam Height Glyph height in pixels.
 * @tparam RowT Unsigned integer type used to store one glyph row. Only the
 * `Width` most significant bits of each row are used (bit `Width-1` is the
 * left-most pixel), mirroring the bit order used by BitmapFont.
 * @tparam RGB_T The pixel color type. Can be RGB565, RGB666, RGB888, etc.
 *
 * Concrete fonts (e.g. converted from vendor font tables) subclass this and
 * pass their own glyph table to the constructor. Only a contiguous ASCII
 * range starting at `firstChar` is supported; any code point outside that
 * range falls back to the glyph for '?'.
 */
template <uint8_t Width, uint8_t Height, typename RowT, typename RGB_T = RGB565>
class FixedBitmapFont : public IFont<RGB_T> {
 public:
  /// Glyph storage: one row entry per pixel row.
  using Glyph = std::array<RowT, Height>;
  /// Unicode code point type used during UTF-8 decoding.
  using CodePoint = uint32_t;

  /// Width of a glyph in pixels.
  static constexpr uint8_t kGlyphWidth = Width;
  /// Height of a glyph in pixels.
  static constexpr uint8_t kGlyphHeight = Height;

  /// Creates a fixed bitmap font over a contiguous glyph table.
  FixedBitmapFont(const Glyph* table, size_t glyphCount, char firstChar = ' ')
      : table_(table), glyphCount_(glyphCount), firstChar_(firstChar) {}

  /// Returns the glyph for an 8-bit character.
  const Glyph& glyph(char character) const {
    return glyph(static_cast<CodePoint>(static_cast<unsigned char>(character)));
  }

  /// Returns the glyph for a Unicode code point.
  const Glyph& glyph(CodePoint codePoint) const {
    const auto first = static_cast<unsigned char>(firstChar_);
    if (codePoint >= first && codePoint < first + glyphCount_) {
      return table_[codePoint - first];
    }
    return replacementGlyph();
  }

  /// Returns whether a pixel is set in a code point glyph.
  bool pixel(CodePoint codePoint, uint8_t x, uint8_t y) const {
    if (x >= kGlyphWidth || y >= kGlyphHeight) {
      return false;
    }
    const RowT rowMask = glyph(codePoint)[y];
    return (rowMask & static_cast<RowT>(RowT(1) << (kGlyphWidth - 1 - x))) != 0;
  }

  /// Draws a single Unicode code point.
  void drawCodePoint(ISurface<RGB_T>& target, int16_t x, int16_t y,
                     CodePoint codePoint, RGB_T foreground,
                     RGB_T background = RGB_T(0), bool opaque = false,
                     uint8_t scale = 1) const {
    if (scale == 0) {
      scale = 1;
    }

    for (uint8_t row = 0; row < kGlyphHeight; ++row) {
      for (uint8_t column = 0; column < kGlyphWidth; ++column) {
        const int16_t pixelX = static_cast<int16_t>(x + (column * scale));
        const int16_t pixelY = static_cast<int16_t>(y + (row * scale));

        if (pixel(codePoint, column, row)) {
          target.fillRect(pixelX, pixelY, scale, scale, foreground);
        } else if (opaque) {
          target.fillRect(pixelX, pixelY, scale, scale, background);
        }
      }
    }
  }

  /// Draws a single character.
  void drawChar(ISurface<RGB_T>& target, int16_t x, int16_t y, char character,
                RGB_T foreground, RGB_T background = RGB_T(0),
                bool opaque = false, uint8_t scale = 1) const {
    drawCodePoint(target, x, y,
                  static_cast<CodePoint>(static_cast<unsigned char>(character)),
                  foreground, background, opaque, scale);
  }

  /// Draws a UTF-8 text string.
  void drawText(ISurface<RGB_T>& target, int16_t x, int16_t y, const char* text,
                RGB_T foreground, RGB_T background = RGB_T(0),
                bool opaque = false, uint8_t scale = 1, uint8_t spacing = 1,
                uint8_t lineSpacing = 1) const override {
    if (text == nullptr) {
      return;
    }
    if (scale == 0) {
      scale = 1;
    }

    const int16_t advanceX =
        static_cast<int16_t>((kGlyphWidth * scale) + spacing);
    const int16_t advanceY =
        static_cast<int16_t>((kGlyphHeight * scale) + lineSpacing);

    int16_t cursorX = x;
    int16_t cursorY = y;
    const char* current = text;
    while (*current != '\0') {
      if (*current == '\n') {
        ++current;
        cursorX = x;
        cursorY = static_cast<int16_t>(cursorY + advanceY);
        continue;
      }

      const CodePoint codePoint = decodeNextUtf8(current);
      drawCodePoint(target, cursorX, cursorY, codePoint, foreground, background,
                    opaque, scale);
      cursorX = static_cast<int16_t>(cursorX + advanceX);
    }
  }

  /// Returns the width of the longest text line in pixels.
  size_t measureTextWidth(const char* text, uint8_t scale = 1,
                          uint8_t spacing = 1) const override {
    if (text == nullptr || *text == '\0') {
      return 0;
    }
    if (scale == 0) {
      scale = 1;
    }

    size_t lineLength = 0;
    size_t longestLine = 0;
    const char* current = text;
    while (*current != '\0') {
      if (*current == '\n') {
        ++current;
        if (lineLength > longestLine) {
          longestLine = lineLength;
        }
        lineLength = 0;
      } else {
        decodeNextUtf8(current);
        ++lineLength;
      }
    }

    if (lineLength > longestLine) {
      longestLine = lineLength;
    }

    if (longestLine == 0) {
      return 0;
    }

    return (longestLine * kGlyphWidth * scale) + ((longestLine - 1) * spacing);
  }

  /// Returns the total text height in pixels.
  size_t measureTextHeight(const char* text, uint8_t scale = 1,
                           uint8_t lineSpacing = 1) const override {
    if (text == nullptr || *text == '\0') {
      return 0;
    }
    if (scale == 0) {
      scale = 1;
    }

    size_t lines = 1;
    for (const char* current = text; *current != '\0'; ++current) {
      if (*current == '\n') {
        ++lines;
      }
    }

    return (lines * kGlyphHeight * scale) + ((lines - 1) * lineSpacing);
  }

  /// Returns the scaled glyph height in pixels.
  size_t getHeight(uint8_t scale) const override {
    return (kGlyphHeight * scale);
  }

 private:
  const Glyph* table_;
  size_t glyphCount_;
  char firstChar_;

  const Glyph& replacementGlyph() const {
    const auto first = static_cast<unsigned char>(firstChar_);
    const auto question = static_cast<unsigned char>('?');
    if (question >= first && question < first + glyphCount_) {
      return table_[question - first];
    }
    return table_[0];
  }

  CodePoint decodeNextUtf8(const char*& current) const {
    const unsigned char firstByte = static_cast<unsigned char>(*current);
    if (firstByte == 0) {
      return 0;
    }

    if ((firstByte & 0x80U) == 0) {
      ++current;
      return static_cast<CodePoint>(firstByte);
    }

    uint8_t sequenceLength = 0;
    CodePoint codePoint = 0;
    if ((firstByte & 0xE0U) == 0xC0U) {
      sequenceLength = 2;
      codePoint = static_cast<CodePoint>(firstByte & 0x1FU);
    } else if ((firstByte & 0xF0U) == 0xE0U) {
      sequenceLength = 3;
      codePoint = static_cast<CodePoint>(firstByte & 0x0FU);
    } else if ((firstByte & 0xF8U) == 0xF0U) {
      sequenceLength = 4;
      codePoint = static_cast<CodePoint>(firstByte & 0x07U);
    } else {
      ++current;
      return static_cast<CodePoint>('?');
    }

    for (uint8_t index = 1; index < sequenceLength; ++index) {
      const unsigned char nextByte = static_cast<unsigned char>(current[index]);
      if (nextByte == 0 || (nextByte & 0xC0U) != 0x80U) {
        ++current;
        return static_cast<CodePoint>('?');
      }

      codePoint = static_cast<CodePoint>((codePoint << 6) | (nextByte & 0x3FU));
    }

    current += sequenceLength;
    return codePoint;
  }
};

}  // namespace tinygpu
