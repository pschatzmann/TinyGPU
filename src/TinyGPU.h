#pragma once
#include "AVIWriter.h"
#include "BMPExporter.h"
#include "BMPParser.h"
#include "CartesianView.h"
#include "FrameBuffer.h"
#include "FrameBufferMonochrome.h"
#include "Surface.h"
#include "SurfaceMonochrome.h"
#include "WireFrame3D.h"

namespace tinygpu {
    

/** 
 *  @typedef SurfaceRGB565
 *  @brief Surface with RGB565 pixel format.
 */
using SurfaceRGB565 = Surface<RGB565>;
/** 
 *  @typedef SurfaceRGB666
 *  @brief Surface with RGB666 pixel format.
 */
using SurfaceRGB666 = Surface<RGB666>;
/** 
 *  @typedef SurfaceRGB888
 *  @brief Surface with RGB888 pixel format.
 */
using SurfaceRGB888 = Surface<RGB888>;
/** 
 *  @typedef SurfaceBGR565
 *  @brief Surface with BGR565 pixel format.
 */
using SurfaceBGR565 = Surface<BGR565>;

/** 
 *  @typedef FrameBufferRGB565
 *  @brief FrameBuffer with RGB565 pixel format.
 */
using FrameBufferRGB565 = FrameBuffer<RGB565>;
/** 
 *  @typedef FrameBufferRGB565
 *  @brief FrameBuffer with RGB565 pixel format.
 */
using FrameBufferRGB565 = FrameBuffer<RGB565>;
/** 
 *  @typedef FrameBufferRGB666
 *  @brief FrameBuffer with RGB666 pixel format.
 */
using FrameBufferRGB666 = FrameBuffer<RGB666>;
/** 
 *  @typedef FrameBufferRGB888
 *  @brief FrameBuffer with RGB888 pixel format.
 */
using FrameBufferRGB888 = FrameBuffer<RGB888>;
/** 
 *  @typedef FrameBufferBGR565
 *  @brief FrameBuffer with BGR565 pixel format.
 */
using FrameBufferBGR565 = FrameBuffer<BGR565>;
/** 
 *  @typedef SpriteRGB565
 *  @brief Sprite with RGB565 pixel format.
 */
using SpriteRGB565 = Sprite<RGB565>;
/** 
 *  @typedef SpriteRGB666
 *  @brief Sprite with RGB666 pixel format.
 */
using SpriteRGB666 = Sprite<RGB666>;
/** 
 *  @typedef SpriteRGB888
 *  @brief Sprite with RGB888 pixel format.
 */
using SpriteRGB888 = Sprite<RGB888>;
/** 
 *  @typedef SpriteBGR565
 *  @brief Sprite with BGR565 pixel format.
 */
using SpriteBGR565 = Sprite<BGR565>;
/** 
 *  @typedef LinePrinterRGB565
 *  @brief LinePrinter with RGB565 pixel format.
 */
using LinePrinterRGB565 = LinePrinter<RGB565>;
/** 
 *  @typedef LinePrinterRGB666
 *  @brief LinePrinter with RGB666 pixel format.
 */
using LinePrinterRGB666 = LinePrinter<RGB666>;
/** @typedef LinePrinterRGB888
 *  @brief LinePrinter with RGB888 pixel format.
 */
using LinePrinterRGB888 = LinePrinter<RGB888>;
/** 
 *  @typedef LinePrinterBGR565
 *  @brief LinePrinter with BGR565 pixel format.
 */
using LinePrinterBGR565 = LinePrinter<BGR565>;
/** 
 *  @typedef BitmapFontRGB565
 *  @brief BitmapFont with RGB565 pixel format.
 */
using BitmapFontRGB565 = BitmapFont<RGB565>;
/** 
 *  @typedef BitmapFontRGB666
 *  @brief BitmapFont with RGB666 pixel format.
 */
using BitmapFontRGB666 = BitmapFont<RGB666>;
/** 
 *  @typedef BitmapFontRGB888
 *  @brief BitmapFont with RGB888 pixel format.
 */
using BitmapFontRGB888 = BitmapFont<RGB888>;
/** @typedef BitmapFontBGR565
 *  @brief BitmapFont with BGR565 pixel format.
 */
using BitmapFontBGR565 = BitmapFont<BGR565>;
/** 
 *  @typedef IFontRGB565
 *  @brief IFont with RGB565 pixel format.
 */
using IFontRGB565 = IFont<RGB565>;
/** 
 *  @typedef IFontRGB666
 *  @brief IFont with RGB666 pixel format.
 */
using IFontRGB666 = IFont<RGB666>;
/** 
 *  @typedef IFontRGB888
 *  @brief IFont with RGB888 pixel format.
 */
using IFontRGB888 = IFont<RGB888>;
/** @typedef IFontBGR565
 *  @brief IFont with BGR565 pixel format.
 */
using IFontBGR565 = IFont<BGR565>;

}  // namespace tinygpu

#ifdef ARDUINO
using namespace tinygpu;
#endif