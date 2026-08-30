#pragma once
#include "TinyGPUConfig.h"
#include "TinyGPU/IO/AVIWriter.h"
#include "TinyGPU/IO/BMPExporter.h"
#include "TinyGPU/IO/BMPParser.h"
#include "TinyGPU/Surface/CartesianView.h"
#include "TinyGPU/Surface/FrameBuffer.h"
#include "TinyGPU/Surface/FrameBufferMonochrome.h"
#include "TinyGPU/Surface/Surface.h"
#include "TinyGPU/Surface/SurfaceMonochrome.h"
#include "TinyGPU/ThreeD/WireFrame3D.h"
#include "TinyGPU/Util/PSRAMAllocator.h"
// TinyGPUGlobals.h (FontRGB565/FontRGB888/FontMonochrome, ...) must come
// before SurfaceWithExternalBuffer.h: that header's constructor default-
// argument (IFont<RGB_T>& font = FontRGB565) needs FontRGB565 already
// declared at the point it's parsed, not merely later in this file.
#include "TinyGPU/Util/TinyGPUGlobals.h"
#include "TinyGPU/Surface/SurfaceWithExternalBuffer.h"
#include <TinyGPU/Drivers/DeviceOutput.h>
#include <TinyGPU/Input/TouchDriver.h>
#include <assert.h>

#if defined(ARDUINO) || defined(TINYGPU_AUTO_NAMESPACE)
using namespace tinygpu;
#endif