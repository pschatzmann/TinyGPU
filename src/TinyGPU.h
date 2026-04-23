#pragma once
#include "TinyGPUConfig.h"
#include "TinyGPU/AVIWriter.h"
#include "TinyGPU/BMPExporter.h"
#include "TinyGPU/BMPParser.h"
#include "TinyGPU/CartesianView.h"
#include "TinyGPU/FrameBuffer.h"
#include "TinyGPU/FrameBufferMonochrome.h"
#include "TinyGPU/Surface.h"
#include "TinyGPU/SurfaceMonochrome.h"
#include "TinyGPU/WireFrame3D.h"
#include "TinyGPU/PSRAMAllocator.h"
#include "TinyGPU/TinyGPUGlobals.h"


#ifdef ARDUINO
using namespace tinygpu;
#endif