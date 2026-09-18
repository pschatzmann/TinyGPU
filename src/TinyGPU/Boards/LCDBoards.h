#pragma once
#ifdef ESP32
#include "TinyGPU/Boards/LCDBoardsESP32.h"
#elif defined(ARDUINO_ARCH_STM32)
#include "TinyGPU/Boards/LCDBoardsSTM32.h"
#elif __has_include(<SDL.h>)
#include "TinyGPU/Boards/LCDBoardsSDL.h"
#else
#include "TinyGPU/Boards/LCDBoardsCommon.h"
#endif