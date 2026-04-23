#pragma once

#include <stddef.h>
#include <stdint.h>

#include <array>
#include "RGB565.h"
#include "RGB888.h"
#include "BGR565.h"
#include "IFont.h"
#include "ISurface.h"

namespace tinygpu {

/**
 * @brief Fixed-size 5x7 bitmap font with ASCII and extended character support.
 *
 * The font can inspect glyph data, measure text, and render UTF-8 strings to
 * any framebuffer implementation that follows the TinyGPU font interface.
 */
template <typename RGB_T = RGB565>
class BitmapFont : public IFont<RGB_T> {
 public:
  /// Glyph storage for one 5x7 character.
  using Glyph = std::array<uint8_t, 7>;
  /// Unicode code point type used during UTF-8 decoding.
  using CodePoint = uint32_t;

  /// Creates a bitmap font instance.
  BitmapFont() = default;

  /// Width of a glyph in pixels.
  static constexpr uint8_t kGlyphWidth = 5;
  /// Height of a glyph in pixels.
  static constexpr uint8_t kGlyphHeight = 7;
  /// First directly stored ASCII character.
  static constexpr char kFirstChar = ' ';
  /// Last directly stored ASCII character.
  static constexpr char kLastChar = '~';

  /// Returns the glyph for an 8-bit character.
  const Glyph& glyph(char character) const {
    return glyph(static_cast<CodePoint>(static_cast<unsigned char>(character)));
  }

  /// Returns the glyph for a Unicode code point.
  const Glyph& glyph(CodePoint codePoint) const {
    if (codePoint >= static_cast<unsigned char>(kFirstChar) &&
        codePoint <= static_cast<unsigned char>(kLastChar)) {
      return glyphTable()[glyphIndex(codePoint)];
    }

    const Glyph* mappedGlyph = glyphForExtendedCodePoint(codePoint);
    return mappedGlyph != nullptr ? *mappedGlyph : replacementGlyph();
  }

  /// Returns whether a pixel is set in an 8-bit character glyph.
  bool pixel(char character, uint8_t x, uint8_t y) const {
    return pixel(static_cast<CodePoint>(static_cast<unsigned char>(character)),
                 x, y);
  }

  /// Returns whether a pixel is set in a code point glyph.
  bool pixel(CodePoint codePoint, uint8_t x, uint8_t y) const {
    if (x >= kGlyphWidth || y >= kGlyphHeight) {
      return false;
    }

    const uint8_t rowMask = glyph(codePoint)[y];
    return (rowMask & static_cast<uint8_t>(1U << (kGlyphWidth - 1 - x))) != 0;
  }

  /// Draws a single character.
  void drawChar(ISurface<RGB_T>& target, int16_t x, int16_t y, char character,
                RGB_T foreground, RGB_T background = RGB_T(0),
                bool opaque = false, uint8_t scale = 1) const {
    drawCodePoint(target, x, y,
                  static_cast<CodePoint>(static_cast<unsigned char>(character)),
                  foreground, background, opaque, scale);
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
                          uint8_t spacing = 1) const {
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
                           uint8_t lineSpacing = 1) const {
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
  size_t getHeight(uint8_t scale) const {
    return (kGlyphHeight * scale);
  }

 private:
  size_t glyphIndex(CodePoint codePoint) const {
    if (codePoint < static_cast<unsigned char>(kFirstChar) ||
        codePoint > static_cast<unsigned char>(kLastChar)) {
      return 0;
    }
    return codePoint - static_cast<unsigned char>(kFirstChar);
  }

  const Glyph& replacementGlyph() const {
    return glyphTable()[glyphIndex(static_cast<CodePoint>('?'))];
  }

  const Glyph& asciiGlyph(char character) const {
    return glyphTable()[glyphIndex(
        static_cast<CodePoint>(static_cast<unsigned char>(character)))];
  }

  const Glyph* asciiGlyphPtr(char character) const {
    return &asciiGlyph(character);
  }

  const Glyph* glyphForExtendedCodePoint(CodePoint codePoint) const {
    switch (codePoint) {
      case 0x80:
      case 0x20AC:
        return &euroGlyph();
      case 0x82:
      case 0x201A:
        return asciiGlyphPtr(',');
      case 0x83:
      case 0x0192:
        return asciiGlyphPtr('f');
      case 0x84:
      case 0x201E:
        return asciiGlyphPtr('"');
      case 0x85:
      case 0x2026:
        return asciiGlyphPtr('.');
      case 0x86:
      case 0x2020:
      case 0x87:
      case 0x2021:
        return asciiGlyphPtr('+');
      case 0x88:
      case 0x02C6:
        return asciiGlyphPtr('^');
      case 0x89:
      case 0x2030:
        return asciiGlyphPtr('%');
      case 0x8A:
      case 0x0160:
        return asciiGlyphPtr('S');
      case 0x8B:
      case 0x2039:
        return asciiGlyphPtr('<');
      case 0x8C:
      case 0x0152:
        return asciiGlyphPtr('O');
      case 0x8E:
      case 0x017D:
        return asciiGlyphPtr('Z');
      case 0x91:
      case 0x2018:
      case 0x92:
      case 0x2019:
        return asciiGlyphPtr('\'');
      case 0x93:
      case 0x201C:
      case 0x94:
      case 0x201D:
        return asciiGlyphPtr('"');
      case 0x95:
      case 0x2022:
        return bulletGlyphPtr();
      case 0x96:
      case 0x2013:
      case 0x97:
      case 0x2014:
        return asciiGlyphPtr('-');
      case 0x98:
      case 0x02DC:
        return asciiGlyphPtr('~');
      case 0x99:
      case 0x2122:
        return asciiGlyphPtr('T');
      case 0x9A:
      case 0x0161:
        return asciiGlyphPtr('s');
      case 0x9B:
      case 0x203A:
        return asciiGlyphPtr('>');
      case 0x9C:
      case 0x0153:
        return asciiGlyphPtr('o');
      case 0x9E:
      case 0x017E:
        return asciiGlyphPtr('z');
      case 0x9F:
      case 0x0178:
        return asciiGlyphPtr('Y');
      case 0xA0:
        return asciiGlyphPtr(' ');
      case 0xA1:
        return asciiGlyphPtr('!');
      case 0xA2:
        return &centGlyph();
      case 0xA3:
        return &sterlingGlyph();
      case 0xA4:
        return asciiGlyphPtr('$');
      case 0xA5:
        return &yenGlyph();
      case 0xA6:
        return asciiGlyphPtr('|');
      case 0xA7:
        return &sectionGlyph();
      case 0xA8:
        return asciiGlyphPtr('"');
      case 0xA9:
        return &copyrightGlyph();
      case 0xAA:
        return asciiGlyphPtr('a');
      case 0xAB:
        return asciiGlyphPtr('<');
      case 0xAC:
        return &notGlyph();
      case 0xAD:
        return asciiGlyphPtr('-');
      case 0xAE:
        return asciiGlyphPtr('R');
      case 0xAF:
        return asciiGlyphPtr('-');
      case 0xB0:
        return &degreeGlyph();
      case 0xB1:
        return &plusMinusGlyph();
      case 0xB2:
        return asciiGlyphPtr('2');
      case 0xB3:
        return asciiGlyphPtr('3');
      case 0xB4:
        return asciiGlyphPtr('\'');
      case 0xB5:
        return &microGlyph();
      case 0xB6:
        return &paragraphGlyph();
      case 0xB7:
        return bulletGlyphPtr();
      case 0xB8:
        return asciiGlyphPtr(',');
      case 0xB9:
        return asciiGlyphPtr('1');
      case 0xBA:
        return asciiGlyphPtr('o');
      case 0xBB:
        return asciiGlyphPtr('>');
      case 0xBC:
      case 0xBD:
      case 0xBE:
        return asciiGlyphPtr('/');
      case 0xBF:
        return asciiGlyphPtr('?');
      case 0xC0:
      case 0xC1:
      case 0xC2:
      case 0xC3:
      case 0xC4:
      case 0xC5:
      case 0xC6:
        return asciiGlyphPtr('A');
      case 0xC7:
        return asciiGlyphPtr('C');
      case 0xC8:
      case 0xC9:
      case 0xCA:
      case 0xCB:
        return asciiGlyphPtr('E');
      case 0xCC:
      case 0xCD:
      case 0xCE:
      case 0xCF:
        return asciiGlyphPtr('I');
      case 0xD0:
        return asciiGlyphPtr('D');
      case 0xD1:
        return asciiGlyphPtr('N');
      case 0xD2:
      case 0xD3:
      case 0xD4:
      case 0xD5:
      case 0xD6:
      case 0xD8:
        return asciiGlyphPtr('O');
      case 0xD7:
        return &multiplyGlyph();
      case 0xD9:
      case 0xDA:
      case 0xDB:
      case 0xDC:
        return asciiGlyphPtr('U');
      case 0xDD:
        return asciiGlyphPtr('Y');
      case 0xDE:
        return asciiGlyphPtr('P');
      case 0xDF:
        return asciiGlyphPtr('B');
      case 0xE0:
      case 0xE1:
      case 0xE2:
      case 0xE3:
      case 0xE4:
      case 0xE5:
      case 0xE6:
        return asciiGlyphPtr('a');
      case 0xE7:
        return asciiGlyphPtr('c');
      case 0xE8:
      case 0xE9:
      case 0xEA:
      case 0xEB:
        return asciiGlyphPtr('e');
      case 0xEC:
      case 0xED:
      case 0xEE:
      case 0xEF:
        return asciiGlyphPtr('i');
      case 0xF0:
        return asciiGlyphPtr('d');
      case 0xF1:
        return asciiGlyphPtr('n');
      case 0xF2:
      case 0xF3:
      case 0xF4:
      case 0xF5:
      case 0xF6:
      case 0xF8:
        return asciiGlyphPtr('o');
      case 0xF7:
        return &divideGlyph();
      case 0xF9:
      case 0xFA:
      case 0xFB:
      case 0xFC:
        return asciiGlyphPtr('u');
      case 0xFD:
      case 0xFF:
        return asciiGlyphPtr('y');
      case 0xFE:
        return asciiGlyphPtr('p');
      default:
        return nullptr;
    }
  }

  const Glyph* bulletGlyphPtr() const {
    static const Glyph glyph = {0b00000, 0b00100, 0b01110, 0b01110,
                                0b01110, 0b00100, 0b00000};
    return &glyph;
  }

  const Glyph& euroGlyph() const {
    static const Glyph glyph = {0b00110, 0b01001, 0b11100, 0b11110,
                                0b11100, 0b01001, 0b00110};
    return glyph;
  }

  const Glyph& centGlyph() const {
    static const Glyph glyph = {0b00100, 0b01110, 0b10100, 0b10000,
                                0b10100, 0b01110, 0b00100};
    return glyph;
  }

  const Glyph& sterlingGlyph() const {
    static const Glyph glyph = {0b00110, 0b01001, 0b01000, 0b11100,
                                0b01000, 0b11111, 0b01000};
    return glyph;
  }

  const Glyph& yenGlyph() const {
    static const Glyph glyph = {0b10001, 0b01010, 0b11111, 0b00100,
                                0b11111, 0b00100, 0b00100};
    return glyph;
  }

  const Glyph& sectionGlyph() const {
    static const Glyph glyph = {0b01110, 0b10000, 0b01100, 0b00010,
                                0b00110, 0b00001, 0b11110};
    return glyph;
  }

  const Glyph& copyrightGlyph() const {
    static const Glyph glyph = {0b01110, 0b10001, 0b10111, 0b10101,
                                0b10111, 0b10001, 0b01110};
    return glyph;
  }

  const Glyph& notGlyph() const {
    static const Glyph glyph = {0b00000, 0b00000, 0b11111, 0b00001,
                                0b00001, 0b00000, 0b00000};
    return glyph;
  }

  const Glyph& degreeGlyph() const {
    static const Glyph glyph = {0b00110, 0b01001, 0b01001, 0b00110,
                                0b00000, 0b00000, 0b00000};
    return glyph;
  }

  const Glyph& plusMinusGlyph() const {
    static const Glyph glyph = {0b00000, 0b00100, 0b00100, 0b11111,
                                0b00100, 0b11111, 0b00000};
    return glyph;
  }

  const Glyph& microGlyph() const {
    static const Glyph glyph = {0b10001, 0b10001, 0b10001, 0b10011,
                                0b01101, 0b10000, 0b10000};
    return glyph;
  }

  const Glyph& paragraphGlyph() const {
    static const Glyph glyph = {0b11110, 0b10110, 0b10110, 0b11110,
                                0b00100, 0b00100, 0b00100};
    return glyph;
  }

  const Glyph& multiplyGlyph() const {
    static const Glyph glyph = {0b00000, 0b10001, 0b01010, 0b00100,
                                0b01010, 0b10001, 0b00000};
    return glyph;
  }

  const Glyph& divideGlyph() const {
    static const Glyph glyph = {0b00000, 0b00100, 0b00000, 0b11111,
                                0b00000, 0b00100, 0b00000};
    return glyph;
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

  const Glyph* glyphTable() const {
    static const Glyph kGlyphs[] = {
        Glyph{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
              0b00000},  // ' '
        Glyph{0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000,
              0b00100},  // '!'
        Glyph{0b01010, 0b01010, 0b01010, 0b00000, 0b00000, 0b00000,
              0b00000},  // '"'
        Glyph{0b01010, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010,
              0b01010},  // '#'
        Glyph{0b00100, 0b01111, 0b10100, 0b01110, 0b00101, 0b11110,
              0b00100},  // '$'
        Glyph{0b11001, 0b11010, 0b00100, 0b01000, 0b10110, 0b00110,
              0b00000},  // '%'
        Glyph{0b01100, 0b10010, 0b10100, 0b01000, 0b10101, 0b10010,
              0b01101},  // '&'
        Glyph{0b00100, 0b00100, 0b00100, 0b00000, 0b00000, 0b00000,
              0b00000},  // '\''
        Glyph{0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100,
              0b00010},  // '('
        Glyph{0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100,
              0b01000},  // ')'
        Glyph{0b00000, 0b10101, 0b01110, 0b11111, 0b01110, 0b10101,
              0b00000},  // '*'
        Glyph{0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100,
              0b00000},  // '+'
        Glyph{0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00100,
              0b01000},  // ','
        Glyph{0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000,
              0b00000},  // '-'
        Glyph{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100,
              0b00100},  // '.'
        Glyph{0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b00000,
              0b00000},  // '/'
        Glyph{0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001,
              0b01110},  // '0'
        Glyph{0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100,
              0b01110},  // '1'
        Glyph{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000,
              0b11111},  // '2'
        Glyph{0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001,
              0b11110},  // '3'
        Glyph{0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010,
              0b00010},  // '4'
        Glyph{0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001,
              0b01110},  // '5'
        Glyph{0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001,
              0b01110},  // '6'
        Glyph{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000,
              0b01000},  // '7'
        Glyph{0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001,
              0b01110},  // '8'
        Glyph{0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010,
              0b01100},  // '9'
        Glyph{0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100,
              0b00000},  // ':'
        Glyph{0b00000, 0b00100, 0b00100, 0b00000, 0b00100, 0b00100,
              0b01000},  // ';'
        Glyph{0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100,
              0b00010},  // '<'
        Glyph{0b00000, 0b00000, 0b11111, 0b00000, 0b11111, 0b00000,
              0b00000},  // '='
        Glyph{0b01000, 0b00100, 0b00010, 0b00001, 0b00010, 0b00100,
              0b01000},  // '>'
        Glyph{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000,
              0b00100},  // '?'
        Glyph{0b01110, 0b10001, 0b00001, 0b01101, 0b10101, 0b10101,
              0b01110},  // '@'
        Glyph{0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001,
              0b10001},  // 'A'
        Glyph{0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001,
              0b11110},  // 'B'
        Glyph{0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001,
              0b01110},  // 'C'
        Glyph{0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010,
              0b11100},  // 'D'
        Glyph{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000,
              0b11111},  // 'E'
        Glyph{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000,
              0b10000},  // 'F'
        Glyph{0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001,
              0b01111},  // 'G'
        Glyph{0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001,
              0b10001},  // 'H'
        Glyph{0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100,
              0b01110},  // 'I'
        Glyph{0b00001, 0b00001, 0b00001, 0b00001, 0b10001, 0b10001,
              0b01110},  // 'J'
        Glyph{0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010,
              0b10001},  // 'K'
        Glyph{0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000,
              0b11111},  // 'L'
        Glyph{0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001,
              0b10001},  // 'M'
        Glyph{0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001,
              0b10001},  // 'N'
        Glyph{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001,
              0b01110},  // 'O'
        Glyph{0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000,
              0b10000},  // 'P'
        Glyph{0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010,
              0b01101},  // 'Q'
        Glyph{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010,
              0b10001},  // 'R'
        Glyph{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001,
              0b11110},  // 'S'
        Glyph{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100,
              0b00100},  // 'T'
        Glyph{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001,
              0b01110},  // 'U'
        Glyph{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010,
              0b00100},  // 'V'
        Glyph{0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101,
              0b01010},  // 'W'
        Glyph{0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001,
              0b10001},  // 'X'
        Glyph{0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100,
              0b00100},  // 'Y'
        Glyph{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000,
              0b11111},  // 'Z'
        Glyph{0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000,
              0b01110},  // '['
        Glyph{0b10000, 0b01000, 0b00100, 0b00010, 0b00001, 0b00000,
              0b00000},  // '\\'
        Glyph{0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010,
              0b01110},  // ']'
        Glyph{0b00100, 0b01010, 0b10001, 0b00000, 0b00000, 0b00000,
              0b00000},  // '^'
        Glyph{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000,
              0b11111},  // '_'
        Glyph{0b01000, 0b00100, 0b00010, 0b00000, 0b00000, 0b00000,
              0b00000},  // '`'
        Glyph{0b00000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001,
              0b01111},  // 'a'
        Glyph{0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001,
              0b11110},  // 'b'
        Glyph{0b00000, 0b00000, 0b01110, 0b10000, 0b10000, 0b10001,
              0b01110},  // 'c'
        Glyph{0b00001, 0b00001, 0b01101, 0b10011, 0b10001, 0b10001,
              0b01111},  // 'd'
        Glyph{0b00000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000,
              0b01110},  // 'e'
        Glyph{0b00110, 0b01001, 0b01000, 0b11100, 0b01000, 0b01000,
              0b01000},  // 'f'
        Glyph{0b00000, 0b00000, 0b01111, 0b10001, 0b10001, 0b01111,
              0b00001},  // 'g'
        Glyph{0b10000, 0b10000, 0b10110, 0b11001, 0b10001, 0b10001,
              0b10001},  // 'h'
        Glyph{0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100,
              0b01110},  // 'i'
        Glyph{0b00010, 0b00000, 0b00110, 0b00010, 0b00010, 0b10010,
              0b01100},  // 'j'
        Glyph{0b10000, 0b10000, 0b10010, 0b10100, 0b11000, 0b10100,
              0b10010},  // 'k'
        Glyph{0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100,
              0b01110},  // 'l'
        Glyph{0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10101,
              0b10101},  // 'm'
        Glyph{0b00000, 0b00000, 0b10110, 0b11001, 0b10001, 0b10001,
              0b10001},  // 'n'
        Glyph{0b00000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001,
              0b01110},  // 'o'
        Glyph{0b00000, 0b00000, 0b11110, 0b10001, 0b11110, 0b10000,
              0b10000},  // 'p'
        Glyph{0b00000, 0b00000, 0b01101, 0b10001, 0b01111, 0b00001,
              0b00001},  // 'q'
        Glyph{0b00000, 0b00000, 0b10110, 0b11001, 0b10000, 0b10000,
              0b10000},  // 'r'
        Glyph{0b00000, 0b00000, 0b01111, 0b10000, 0b01110, 0b00001,
              0b11110},  // 's'
        Glyph{0b01000, 0b01000, 0b11100, 0b01000, 0b01000, 0b01001,
              0b00110},  // 't'
        Glyph{0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10011,
              0b01101},  // 'u'
        Glyph{0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b01010,
              0b00100},  // 'v'
        Glyph{0b00000, 0b00000, 0b10001, 0b10001, 0b10101, 0b10101,
              0b01010},  // 'w'
        Glyph{0b00000, 0b00000, 0b10001, 0b01010, 0b00100, 0b01010,
              0b10001},  // 'x'
        Glyph{0b00000, 0b00000, 0b10001, 0b10001, 0b01111, 0b00001,
              0b11110},  // 'y'
        Glyph{0b00000, 0b00000, 0b11111, 0b00010, 0b00100, 0b01000,
              0b11111},  // 'z'
        Glyph{0b00010, 0b00100, 0b00100, 0b01000, 0b00100, 0b00100,
              0b00010},  // '{'
        Glyph{0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100,
              0b00100},  // '|'
        Glyph{0b01000, 0b00100, 0b00100, 0b00010, 0b00100, 0b00100,
              0b01000},  // '}'
        Glyph{0b00000, 0b00000, 0b01001, 0b10110, 0b00000, 0b00000,
              0b00000},  // '~'
    };

    return kGlyphs;
  }
};

static BitmapFont<RGB565> FontRGB565;
static BitmapFont<RGB888> FontRGB888;
static BitmapFont<BGR565> FontBGR565;
static BitmapFont<bool> FontMonochrome;

}  // namespace tinygpu