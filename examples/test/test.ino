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
FrameBuffer<BGR565> framebuffer3(8, 8, FontBGR565);
FrameBufferRGB565 framebuffer4(8, 8, FontRGB565);
FrameBufferRGB888 framebuffer5(8, 8, FontRGB888);
FrameBufferBGR565 framebuffer6(8, 8, FontBGR565);
FrameBufferMonochrome framebuffer7(8, 8, FontMonochrome);

Surface<RGB565> surface1(8, 8, FontRGB565);
Surface<RGB888> surface2(8, 8, FontRGB888);
Surface<BGR565> surface3(8, 8, FontBGR565);
SurfaceRGB565 surface4(8, 8, FontRGB565);
SurfaceRGB888 surface5(8, 8, FontRGB888);
SurfaceBGR565 surface6(8, 8, FontBGR565);
SurfaceMonochrome surface7(8, 8, FontMonochrome);

Sprite<RGB565> sprite1(8, 8, FontRGB565);
Sprite<RGB888> sprite2(8, 8, FontRGB888);
Sprite<BGR565> sprite3(8, 8, FontBGR565);
SpriteRGB565 sprite4(8, 8, FontRGB565);
SpriteRGB888 sprite5(8, 8, FontRGB888);
SpriteBGR565 sprite6(8, 8, FontBGR565);
SpriteMonochrome sprite7(8, 8, FontMonochrome);


void setup(){}
void loop(){}