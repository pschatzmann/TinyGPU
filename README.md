# TinyGPU

TinyGPU is a lightweight Arduino graphics library for RGB565 bitmap surfaces, sprites, and simple 3D wireframe rendering.

RGB565 is a compact 16-bit color format that stores red in 5 bits, green in 6 bits, and blue in 5 bits. It is widely used by small TFT, LCD, OLED, and other embedded display controllers because it needs much less memory and bandwidth than 24-bit RGB while still providing good visual quality for many graphics
applications.

## Features

- RGB565 color 
- In-memory bitmap surfaces
- Basic drawing primitives
  - pixels
  - lines
  - rectangles
  - circles
- Bitmap font rendering
- Wrapped line printing
- Sprite drawing and sprite-aware framebuffer management
  - add
  - move
  - scale
  - rotate
- Incremental BMP image decoding into RGB565 surfaces
- Basic 3D wireframe rendering
  - transforms
  - camera / view matrix
  - perspective and orthographic projection
  - minimal depth-buffered line rendering
- Arduino example sketches

## Overview

TinyGPU is designed as a small in-memory rendering layer that stays independent from any specific display driver. You render into RGB565 memory first and then forward the resulting pixel data to your own hardware-specific output code.

The library covers three main areas:

- 2D drawing and text rendering for compact embedded displays
- sprite-oriented composition and transforms for UI and simple animation
- lightweight 3D wireframe rendering for visualizations and demos

It also includes incremental BMP decoding so image assets can be converted into display-ready RGB565 surfaces without requiring a desktop preprocessing step.

## Include Files

Most drawing functionality is available through the main umbrella header:

- `TinyGPU.h`

BMP decoding is provided separately through:

- `BMPParser.h`

## Documentaion

- [Class Documentation](hthttps://pschatzmann.github.io/TinyGPU/namespacetinygpu.html)
- [examples](examples)


## Sending Pixels to a Real Display

TinyGPU keeps all drawing in memory. After rendering, you can send the raw
RGB565 data to your display driver.

Useful accessors are:

- `data()` for byte-wise access
- `size()` for total byte size

This separation keeps the drawing API independent from any specific display
controller or transport.

