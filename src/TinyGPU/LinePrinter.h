#pragma once

#include <string>

#include "IFont.h"
#include "ISurface.h"

namespace tinygpu {

/**
 * @brief Helper for printing wrapped lines of text onto a TinyGPU target.
 *
 * The printer tracks a cursor position, applies configurable borders, and
 * wraps words to the next line when text would exceed the drawable width.
 */
template <typename RGB_T = RGB565>
class LinePrinter {
 public:
  LinePrinter() = default;
  /// Creates a line printer for the given font and target.
  LinePrinter(IFont<RGB_T>& font, ISurface<RGB_T>& target) : font_(&font), target_(&target) {
    currentX_ = leftBorder_;
    currentY_ = topBorder_;
  }

  void setColor(RGB_T color) { foregroundColor_ = color; }
  void setBackgroundColor(RGB_T color) { backgroundColor_ = color; }

  /// Sets the Font implementation.
  void setFont(IFont<RGB_T>& font) { font_ = &font; }
  /// Sets the TinyGPU target.
  void setTarget(ISurface<RGB_T>& target) { target_ = &target; }

  /// Sets the top border in pixels.
  void setTopBorder(size_t border) { topBorder_ = border; }
  /// Sets the left border in pixels.
  void setLeftBorder(size_t border) { leftBorder_ = border; }
  /// Sets the right border in pixels.
  void setRightBorder(size_t border) { rightBorder_ = border; }
  /// Sets the bottom border in pixels.
  void setButtomBorder(size_t border) { buttomBorder_ = border; }
  /// Sets all borders to the same value.
  void setBorders(size_t border) {
    setTopBorder(border);
    setLeftBorder(border);
    setRightBorder(border);
    setButtomBorder(border);
  }

  /// Sets the current line index.
  void setActualLine(size_t line) {
    currentLine_ = line;
    currentX_ = leftBorder_;
    currentY_ = topBorder_ + (line * lineAdvance());
  }

  /// Sets the text scale factor.
  void setScale(uint8_t scale) { this->scale = scale; }

  /// Sets the spacing between glyphs.
  void setSpacing(uint8_t spacing) { this->spacing = spacing; }

  /// Prints text with word wrapping.
  size_t print(const char* text) {
    if (text == nullptr || *text == '\0') {
      return 0;
    }

    ensureCursorInitialized();

    std::string word;
    size_t pendingSpaces = 0;
    size_t printedLength = 0;

    for (const char* current = text; *current != '\0'; ++current) {
      if (*current == '\n') {
        printWord(word, pendingSpaces);
        pendingSpaces = 0;
        nextLine();
        ++printedLength;
        continue;
      }

      if (*current == ' ') {
        printWord(word, pendingSpaces);
        ++pendingSpaces;
        ++printedLength;
        continue;
      }

      if (*current == '\t') {
        printWord(word, pendingSpaces);
        pendingSpaces += 4;
        ++printedLength;
        continue;
      }

      word.push_back(*current);
      ++printedLength;
    }

    printWord(word, pendingSpaces);
    flushPendingSpaces(pendingSpaces);
    return printedLength;
  }

  /// Prints text and advances to the next line.
  size_t println(const char* text) {
    const size_t printedLength = print(text);
    nextLine();
    return printedLength;
  }

  /// Advances to the next line.
  size_t println() {
    nextLine();
    return 0;
  }

private:
  IFont<RGB_T>* font_ = nullptr;
  RGB_T backgroundColor_ = RGB_T();
#if __cplusplus >= 201703L
  // C++17 if constexpr for type-specific initialization
  RGB_T foregroundColor_ = []{
    if constexpr (std::is_same<RGB_T, bool>::value) return true;
    else return RGB_T(255, 255, 255);
  }();
#else
  RGB_T foregroundColor_ = std::is_same<RGB_T, bool>::value ? true : RGB_T(255, 255, 255);
#endif
  ISurface<RGB_T>* target_ = nullptr;
  size_t topBorder_ = 20;
  size_t leftBorder_ = 20;
  size_t rightBorder_ = 20;
  size_t buttomBorder_ = 20;
  size_t currentLine_ = 0;
  size_t currentY_ = 0;
  size_t currentX_ = 0;
  uint8_t scale = 1;
  uint8_t spacing = 1;

  void ensureCursorInitialized() {
    if (currentX_ == 0 && currentY_ == 0) {
      currentX_ = leftBorder_;
      currentY_ = topBorder_;
    }
  }

  size_t drawableRightEdge() const {
    if (target_ == nullptr) {
      return 0;
    }
    const size_t targetWidth = target_->width();
    return targetWidth > rightBorder_ ? targetWidth - rightBorder_ : 0;
  }

  bool isAtLineStart() const { return currentX_ <= leftBorder_; }

  size_t lineAdvance() const {
    return font_->measureTextHeight("A", scale, 0) + spacing;
  }

  void nextLine() {
    currentX_ = leftBorder_;
    currentY_ += lineAdvance();
    ++currentLine_;
  }

  void printChunk(const std::string& chunk) {
    if (chunk.empty()) {
      return;
    }

    target_->drawText(static_cast<int16_t>(currentX_),
                      static_cast<int16_t>(currentY_), chunk.c_str(),
                      foregroundColor_, backgroundColor_, false, scale,
                      spacing);
    currentX_ += font_->measureTextWidth(chunk.c_str(), scale, spacing);
  }

  size_t pendingSpaceWidth(size_t pendingSpaces) const {
    if (pendingSpaces == 0) {
      return 0;
    }

    const std::string spaces(pendingSpaces, ' ');
    return font_->measureTextWidth(spaces.c_str(), scale, spacing);
  }

  void flushPendingSpaces(size_t& pendingSpaces) {
    if (pendingSpaces == 0 || isAtLineStart()) {
      pendingSpaces = 0;
      return;
    }

    const size_t spacesWidth = pendingSpaceWidth(pendingSpaces);
    if ((currentX_ + spacesWidth) > drawableRightEdge()) {
      nextLine();
      pendingSpaces = 0;
      return;
    }

    printChunk(std::string(pendingSpaces, ' '));
    pendingSpaces = 0;
  }

  void printWord(std::string& word, size_t& pendingSpaces) {
    if (word.empty()) {
      return;
    }

    const std::string leadingSpaces =
        isAtLineStart() ? std::string() : std::string(pendingSpaces, ' ');
    const std::string chunk = leadingSpaces + word;
    const size_t chunkWidth =
        font_->measureTextWidth(chunk.c_str(), scale, spacing);

    if (!isAtLineStart() && (currentX_ + chunkWidth) > drawableRightEdge()) {
      nextLine();
    }

    if (!isAtLineStart() && pendingSpaces > 0) {
      printChunk(leadingSpaces);
    }

    printChunk(word);
    pendingSpaces = 0;
    word.clear();
  }
};

}  // namespace tinygpu