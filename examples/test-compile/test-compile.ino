/**
 * @file test.ino
 * @brief TinyGPU API type alias and template instantiation test.
 *
 * This test instantiates all FrameBuffer, Surface, and Sprite types
 * (both template and alias forms) for all supported color types.
 *
 * Use this file to verify that all type aliases and template instantiations
 * compile and link correctly with the current TinyGPU API.
 */

#include <TinyGPU.h>

FrameBuffer<RGB565> framebuffer1(8, 8, FontRGB565);
FrameBuffer<RGB888> framebuffer2(8, 8, FontRGB888);
FrameBufferRGB565 framebuffer4(8, 8, FontRGB565);
FrameBufferRGB888 framebuffer5(8, 8, FontRGB888);
FrameBufferMonochrome framebuffer7(8, 8, FontMonochrome);

Surface<RGB565> surface1(8, 8, FontRGB565);
Surface<RGB888> surface2(8, 8, FontRGB888);
SurfaceRGB565 surface4(8, 8, FontRGB565);
SurfaceRGB888 surface5(8, 8, FontRGB888);
SurfaceMonochrome surface7(8, 8, FontMonochrome);

Sprite<RGB565> sprite1(8, 8, FontRGB565);
Sprite<RGB888> sprite2(8, 8, FontRGB888);
SpriteRGB565 sprite4(8, 8, FontRGB565);
SpriteRGB888 sprite5(8, 8, FontRGB888);
SpriteMonochrome sprite7(8, 8, FontMonochrome);

// STM32-EVAL fonts.c conversions
Font8x8<RGB565> font8x8;
Font8x12<RGB565> font8x12;
Font12x12<RGB565> font12x12;
Font16x24<RGB565> font16x24;
Font8x8RGB565 font8x8b;
Font8x12RGB888 font8x12b;
Font16x24RGB666 font16x24b;
FrameBuffer<RGB565> framebuffer8(16, 24, font16x24);

void setup(){}
void loop(){}