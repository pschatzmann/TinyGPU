#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ISurface.h"
#include "Print.h"

namespace tinygpu {

/**
 * @brief Writes a simple AVI movie containing RGB565 frames.
 *
 * The implementation writes frames directly to the output stream as they are
 * added. When the expected frame count is provided in begin(), the AVI headers
 * and optional idx1 index can be precomputed without buffering the frame data.
 * Frames are encoded as uncompressed 16-bit BI_BITFIELDS video using RGB565
 * channel masks.
 * 
 */
template <typename RGB_T = RGB565>
class AVIWriter {
 public:
  /// Creates an AVI writer that streams data to the provided Print output.
  AVIWriter(Print& out) : out_(out) {}
  /// Finalizes the AVI stream if it is still open.
  ~AVIWriter() { end(); }

  /// Starts a new AVI stream using the surface dimensions and optional frame
  /// count.
  bool begin(const ISurface<RGB_T>& surface, uint16_t fps = 30,
             uint32_t frameCount = 0) {
    end();

    width_ = static_cast<uint32_t>(surface.width());
    height_ = static_cast<uint32_t>(surface.height());
    if (width_ == 0 || height_ == 0) {
      resetState();
      return false;
    }

    fps_ = fps == 0 ? 1 : fps;
    rowStride_ = frameRowStride(width_);
    frameSize_ = rowStride_ * height_;
    expectedFrameCount_ = frameCount;
    frameCount_ = 0;
    isOpen_ = true;

    writeHeader();
    return add(surface);
  }

  /// Finalizes the AVI stream and writes the optional frame index.
  void end() {
    if (!isOpen_) {
      return;
    }

    if (expectedFrameCount_ != 0) {
      writeIndex();
    }
    resetState();
  }

  /// Appends one RGB565 frame to the AVI stream.
  bool add(const ISurface<RGB_T>& surface) {
    if (!isOpen_ || surface.width() != width_ || surface.height() != height_) {
      return false;
    }

    if (expectedFrameCount_ != 0 && frameCount_ >= expectedFrameCount_) {
      return false;
    }

    writeFourCC("00db");
    writeU32(frameSize_);
    writeFrame(surface);
    if ((frameSize_ & 0x1U) != 0U) {
      writeU8(0);
    }

    ++frameCount_;
    return true;
  }

 protected:
  Print& out_;
  uint32_t width_ = 0;
  uint32_t height_ = 0;
  uint16_t fps_ = 30;
  uint32_t rowStride_ = 0;
  uint32_t frameSize_ = 0;
  uint32_t expectedFrameCount_ = 0;
  uint32_t frameCount_ = 0;
  bool isOpen_ = false;

  /// Returns the padded byte stride of one RGB565 frame row.
  static uint32_t frameRowStride(uint32_t width) {
    return ((width * 2U) + 3U) & ~0x3U;
  }

  /// Returns the data size rounded up to an even AVI chunk length.
  static uint32_t paddedChunkSize(uint32_t dataSize) {
    return dataSize + (dataSize & 0x1U);
  }

  /// Resets the writer state after finishing or aborting a stream.
  void resetState() {
    width_ = 0;
    height_ = 0;
    fps_ = 30;
    rowStride_ = 0;
    frameSize_ = 0;
    expectedFrameCount_ = 0;
    frameCount_ = 0;
    isOpen_ = false;
  }

  /// Writes the RIFF, AVI, stream, and movi headers.
  void writeHeader() {
    const uint32_t avihSize = 56;
    const uint32_t strhSize = 56;
    const uint32_t strfSize = 52;
    const uint32_t avihChunkSize = 8 + avihSize;
    const uint32_t strhChunkSize = 8 + strhSize;
    const uint32_t strfChunkSize = 8 + strfSize;
    const uint32_t strlListSize = 4 + strhChunkSize + strfChunkSize;
    const uint32_t strlChunkSize = 8 + strlListSize;
    const uint32_t hdrlListSize = 4 + avihChunkSize + strlChunkSize;
    const uint32_t hdrlChunkSize = 8 + hdrlListSize;
    const uint32_t frameChunkSize = 8 + paddedChunkSize(frameSize_);
    const uint32_t headerFrameCount = expectedFrameCount_;
    const uint32_t moviListSize =
        expectedFrameCount_ == 0 ? 4
                                 : 4 + (frameChunkSize * expectedFrameCount_);
    const uint32_t moviChunkSize = 8 + moviListSize;
    const uint32_t idx1Size =
        expectedFrameCount_ == 0 ? 0 : expectedFrameCount_ * 16U;
    const uint32_t idx1ChunkSize = expectedFrameCount_ == 0 ? 0 : 8 + idx1Size;
    const uint32_t riffSize =
        expectedFrameCount_ == 0
            ? 4 + hdrlChunkSize + moviChunkSize
            : 4 + hdrlChunkSize + moviChunkSize + idx1ChunkSize;
    const uint32_t microsecondsPerFrame = 1000000UL / fps_;
    const uint32_t maxBytesPerSecond = frameSize_ * fps_;
    const uint32_t suggestedBufferSize = frameSize_;

    writeFourCC("RIFF");  // RIFF chunk id
    writeU32(riffSize);   // RIFF chunk size
    writeFourCC("AVI ");  // AVI form type

    writeFourCC("LIST");     // LIST chunk id
    writeU32(hdrlListSize);  // hdrl list size
    writeFourCC("hdrl");     // header list type

    writeFourCC("avih");             // main AVI header chunk id
    writeU32(avihSize);              // main AVI header size
    writeU32(microsecondsPerFrame);  // dwMicroSecPerFrame
    writeU32(maxBytesPerSecond);     // dwMaxBytesPerSec
    writeU32(0);                     // dwPaddingGranularity
    writeU32(0x10);                  // dwFlags (AVIF_HASINDEX when indexed)
    writeU32(headerFrameCount);      // dwTotalFrames
    writeU32(0);                     // dwInitialFrames
    writeU32(1);                     // dwStreams
    writeU32(suggestedBufferSize);   // dwSuggestedBufferSize
    writeU32(width_);                // dwWidth
    writeU32(height_);               // dwHeight
    writeU32(0);                     // dwReserved[0]
    writeU32(0);                     // dwReserved[1]
    writeU32(0);                     // dwReserved[2]
    writeU32(0);                     // dwReserved[3]

    writeFourCC("LIST");     // LIST chunk id
    writeU32(strlListSize);  // strl list size
    writeFourCC("strl");     // stream list type

    writeFourCC("strh");                       // stream header chunk id
    writeU32(strhSize);                        // stream header size
    writeFourCC("vids");                       // fccType = video stream
    writeFourCC("DIB ");                       // fccHandler = uncompressed DIB
    writeU32(0);                               // dwFlags
    writeU16(0);                               // wPriority
    writeU16(0);                               // wLanguage
    writeU32(0);                               // dwInitialFrames
    writeU32(1);                               // dwScale
    writeU32(fps_);                            // dwRate
    writeU32(0);                               // dwStart
    writeU32(headerFrameCount);                // dwLength
    writeU32(suggestedBufferSize);             // dwSuggestedBufferSize
    writeU32(0xFFFFFFFFUL);                    // dwQuality
    writeU32(0);                               // dwSampleSize
    writeU16(0);                               // rcFrame.left
    writeU16(0);                               // rcFrame.top
    writeU16(static_cast<uint16_t>(width_));   // rcFrame.right
    writeU16(static_cast<uint16_t>(height_));  // rcFrame.bottom

    writeFourCC("strf");   // stream format chunk id
    writeU32(strfSize);    // stream format size
    writeU32(40);          // biSize
    writeU32(width_);      // biWidth
    writeU32(height_);     // biHeight
    writeU16(1);           // biPlanes
    writeU16(16);          // biBitCount
    writeU32(3);           // biCompression = BI_BITFIELDS
    writeU32(frameSize_);  // biSizeImage
    writeU32(0);           // biXPelsPerMeter
    writeU32(0);           // biYPelsPerMeter
    writeU32(0);           // biClrUsed
    writeU32(0);           // biClrImportant
    writeU32(0xF800);      // red mask
    writeU32(0x07E0);      // green mask
    writeU32(0x001F);      // blue mask

    writeFourCC("LIST");     // LIST chunk id
    writeU32(moviListSize);  // movi list size
    writeFourCC("movi");     // movi list type
  }

  /// Writes one surface as a bottom-up padded RGB565 AVI frame.
  void writeFrame(const ISurface<RGB_T>& surface) {
    const uint32_t width = static_cast<uint32_t>(surface.width());
    const uint32_t height = static_cast<uint32_t>(surface.height());
    const uint32_t rowStride = frameRowStride(width);
    const uint8_t padByte = 0;

    for (uint32_t row = 0; row < height; ++row) {
      const uint32_t sourceY = height - 1U - row;
      for (uint32_t x = 0; x < width; ++x) {
        writeU16(surface.getPixel(x, sourceY).getValue());
      }
      for (uint32_t padding = width * 2U; padding < rowStride; ++padding) {
        writeBytes(&padByte, 1);
      }
    }
  }

  /// Writes the idx1 frame index when a frame count was provided.
  void writeIndex() {
    const uint32_t frameDataSize = frameSize_;
    const uint32_t idx1Size = expectedFrameCount_ * 16U;

    writeFourCC("idx1");
    writeU32(idx1Size);

    uint32_t moviOffset = 4;
    for (uint32_t frame = 0; frame < expectedFrameCount_; ++frame) {
      writeFourCC("00db");
      writeU32(0x10);
      writeU32(moviOffset);
      writeU32(frameDataSize);
      moviOffset += 8 + paddedChunkSize(frameDataSize);
    }
  }

  /// Writes a four-character AVI chunk identifier.
  void writeFourCC(const char* value) {
    writeBytes(reinterpret_cast<const uint8_t*>(value), 4);
  }

  /// Writes a single byte to the output stream.
  void writeU8(uint8_t value) { out_.write(value); }

  /// Writes a 16-bit little-endian value to the output stream.
  void writeU16(uint16_t value) {
    const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xFFU),
                              static_cast<uint8_t>((value >> 8) & 0xFFU)};
    writeBytes(bytes, sizeof(bytes));
  }

  /// Writes a 32-bit little-endian value to the output stream.
  void writeU32(uint32_t value) {
    const uint8_t bytes[4] = {static_cast<uint8_t>(value & 0xFFU),
                              static_cast<uint8_t>((value >> 8) & 0xFFU),
                              static_cast<uint8_t>((value >> 16) & 0xFFU),
                              static_cast<uint8_t>((value >> 24) & 0xFFU)};
    writeBytes(bytes, sizeof(bytes));
  }

  /// Writes a raw byte sequence to the output stream.
  void writeBytes(const uint8_t* data, size_t length) {
    out_.write(data, length);
  }
};

}  // namespace tinygpu