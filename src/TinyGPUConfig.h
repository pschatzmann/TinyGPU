#pragma once

#include <vector>
#include "TinyGPU/PSRAMAllocator.h"

/// Activate ESP32-S3-specific optimizations 
#ifndef TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS
#if defined(CONFIG_IDF_TARGET_ESP32S3) 
#define TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS 1
#else
#define TINYGPU_ENABLE_ESP32S3_OPTIMIZATIONS 0
#endif
#endif

/// Use PSRAM if available for large buffers to save internal RAM
#ifndef TINYGPU_USE_PSRAM_ALLOCATOR
#define TINYGPU_USE_PSRAM_ALLOCATOR 1
#endif


