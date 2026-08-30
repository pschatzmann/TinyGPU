#pragma once

#include <stddef.h>
#include <stdint.h>

#include <array>

#include "TinyGPU/Font/IFont.h"
#include "TinyGPU/Surface/ISurface.h"
#include "TinyGPU/Color/RGB565.h"

namespace tinygpu {

/**
 * @brief Fixed-size bitmap font backed by a caller-supplied glyph table.
 *
 * @tparam Width Glyph width in pixels.
 * @tparam Height Glyph height in pixels.
 * @tparam RowT Unsigned integer type used to store one glyph row. Only the
 * `Width` most significant bits of each row are used (bit `Width-1` is the
 * left-most pixel), mirroring the bit order used by Font5x7.
 * @tparam RGB_T The pixel color type. Can be RGB565, RGB666, RGB888, etc.
 *
 * Concrete fonts (e.g. converted from vendor font tables) subclass this and
 * pass their own glyph table to the constructor. Only a contiguous ASCII
 * range starting at `firstChar` is supported; any code point outside that
 * range falls back to the glyph for '?'.
 */
template <uint8_t Width, uint8_t Height, typename RowT, typename RGB_T = RGB565>
class BitmapFont : public IFont<RGB_T> {
 public:
  /// Glyph storage: one row entry per pixel row.
  using Glyph = std::array<RowT, Height>;
  /// Unicode code point type used during UTF-8 decoding.
  using CodePoint = uint32_t;

  /// Width of a glyph in pixels.
  static constexpr uint8_t kGlyphWidth = Width;
  /// Height of a glyph in pixels.
  static constexpr uint8_t kGlyphHeight = Height;
  /// Advance width used in proportional mode for glyphs with no set pixels
  /// (e.g. the space character).
  static constexpr uint8_t kBlankGlyphWidth = (kGlyphWidth > 2) ? kGlyphWidth - 2 : kGlyphWidth;

  /// Creates a fixed bitmap font over a contiguous glyph table.
  BitmapFont(const Glyph* table, size_t glyphCount, char firstChar = ' ')
      : table_(table), glyphCount_(glyphCount), firstChar_(firstChar) {}

  /// Enables/disables proportional spacing based on each glyph's effective
  /// (used) width instead of the fixed glyph width.
  void setProportional(bool proportional) override { proportional_ = proportional; }
  /// Returns whether proportional spacing is enabled.
  bool isProportional() const override { return proportional_; }

  /// Returns the left-most column (0-based) that has a set pixel in a code
  /// point's glyph. Glyphs with no set pixels (e.g. the space) return 0.
  uint8_t glyphLeftOffset(CodePoint codePoint) const {
    uint8_t left = 0;
    uint8_t right = 0;
    glyphColumnBounds(codePoint, left, right);
    return left;
  }

  /// Returns the effective (used) width in pixels of a code point's glyph,
  /// i.e. the span from its left-most to right-most set pixel column,
  /// inclusive. Glyphs with no set pixels (e.g. the space) fall back to a
  /// default blank width.
  uint8_t glyphEffectiveWidth(CodePoint codePoint) const {
    uint8_t left = 0;
    uint8_t right = 0;
    if (!glyphColumnBounds(codePoint, left, right)) {
      return kBlankGlyphWidth;
    }
    return static_cast<uint8_t>(right - left + 1);
  }

  /// Returns the advance width (in unscaled pixels) used to move the cursor
  /// past a code point's glyph, honoring proportional mode when enabled.
  uint8_t glyphAdvanceWidth(CodePoint codePoint) const {
    return proportional_ ? glyphEffectiveWidth(codePoint) : kGlyphWidth;
  }

  /// Returns the glyph for an 8-bit character.
  const Glyph& glyph(char character) const {
    return glyph(static_cast<CodePoint>(static_cast<unsigned char>(character)));
  }

  /// Returns the glyph for a Unicode code point. Virtual so a subclass
  /// with its own extra fallback logic (e.g. Font5x7's mapping of
  /// Latin-1/CP1252-ish codepoints outside its stored ASCII range) can
  /// override just this lookup and get every other method here - pixel(),
  /// drawText(), measureTextWidth(), ... - for free, since they all call
  /// glyph() internally rather than duplicating the lookup themselves.
  virtual const Glyph& glyph(CodePoint codePoint) const {
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

  /// Draws a single Unicode code point. In proportional mode, leading blank
  /// columns of the glyph are skipped so the visible pixels start at `x`.
  void drawCodePoint(ISurface<RGB_T>& target, int16_t x, int16_t y,
                     CodePoint codePoint, RGB_T foreground,
                     RGB_T background = RGB_T(0), bool opaque = false,
                     uint8_t scale = 1) const {
    if (scale == 0) {
      scale = 1;
    }

    uint8_t startColumn = 0;
    uint8_t endColumn = kGlyphWidth;
    if (proportional_) {
      uint8_t left = 0;
      uint8_t right = 0;
      if (glyphColumnBounds(codePoint, left, right)) {
        startColumn = left;
        endColumn = static_cast<uint8_t>(right + 1);
      } else {
        endColumn = kBlankGlyphWidth;
      }
    }

    for (uint8_t row = 0; row < kGlyphHeight; ++row) {
      for (uint8_t column = startColumn; column < endColumn; ++column) {
        const int16_t pixelX =
            static_cast<int16_t>(x + ((column - startColumn) * scale));
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
      const int16_t advanceX = static_cast<int16_t>(
          (glyphAdvanceWidth(codePoint) * scale) + spacing);
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

    size_t lineWidth = 0;
    size_t lineChars = 0;
    size_t longestLineWidth = 0;
    const char* current = text;
    while (*current != '\0') {
      if (*current == '\n') {
        ++current;
        if (lineWidth > longestLineWidth) {
          longestLineWidth = lineWidth;
        }
        lineWidth = 0;
        lineChars = 0;
      } else {
        const CodePoint codePoint = decodeNextUtf8(current);
        if (lineChars > 0) {
          lineWidth += spacing;
        }
        lineWidth += static_cast<size_t>(glyphAdvanceWidth(codePoint)) * scale;
        ++lineChars;
      }
    }

    if (lineWidth > longestLineWidth) {
      longestLineWidth = lineWidth;
    }

    return longestLineWidth;
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
  bool proportional_ = true;

  /// Finds the left-most and right-most columns (0-based, inclusive) that
  /// have a set pixel in a code point's glyph. Returns false, leaving
  /// `leftColumn`/`rightColumn` unchanged, if the glyph has no set pixels.
  bool glyphColumnBounds(CodePoint codePoint, uint8_t& leftColumn,
                         uint8_t& rightColumn) const {
    const Glyph& glyphData = glyph(codePoint);
    bool any = false;
    uint8_t left = kGlyphWidth;
    uint8_t right = 0;
    for (uint8_t row = 0; row < kGlyphHeight; ++row) {
      const RowT rowMask = glyphData[row];
      if (rowMask == 0) {
        continue;
      }
      for (uint8_t column = 0; column < kGlyphWidth; ++column) {
        if ((rowMask & static_cast<RowT>(RowT(1) << (kGlyphWidth - 1 - column))) !=
            0) {
          any = true;
          if (column < left) {
            left = column;
          }
          if (column > right) {
            right = column;
          }
        }
      }
    }

    if (!any) {
      return false;
    }
    leftColumn = left;
    rightColumn = right;
    return true;
  }

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
