#pragma once

#include "TinyGPU/Surface/FrameBuffer.h"
#include "TinyGPU/Surface/SurfaceMonochrome.h"

namespace tinygpu {

/// @brief FrameBufferMonochrome is a framebuffer specialized for 1-bit (monochrome) pixel storage.
///
/// This alias provides a convenient way to use FrameBuffer with SurfaceMonochrome for efficient
/// bit-packed monochrome graphics operations.
using FrameBufferMonochrome = FrameBuffer<bool, SurfaceMonochrome>;

}