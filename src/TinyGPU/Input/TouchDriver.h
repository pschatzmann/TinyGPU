#pragma once

#ifdef ARDUINO
#include <TinyGPU/Input/TouchDriverArduino.h>
#elif __has_include(<SDL.h>)
#include <TinyGPU/Input/TouchDriverSDL.h>
#else 
#include <TinyGPU/Input/TouchDriverCommon.h>
#endif
