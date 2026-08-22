# TinyGPU

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![License: Apache](https://img.shields.io/badge/License-Apache-yellow.svg)](https://opensource.org/licenses/Apache-2.0)

TinyGPU is a lightweight Arduino graphics library for RGB565 bitmap surfaces, sprites, and simple 3D wireframe rendering.

![Watch Wireframe](docs/rotating-cube.gif)

RGB565 is a compact 16-bit color format that stores red in 5 bits, green in 6 bits, and blue in 5 bits. It is widely used by small TFT, LCD, OLED, and other embedded display controllers because it needs much less memory and bandwidth than 24-bit RGB while still providing good visual quality for many graphics applications.

Apart form RGB565 we also support RGB666, RGB888 and Monochrome.

## Features

- RGB565, RGB666, RGB888 and Monochrome color 
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
- Basic 3D wireframe rendering
  - transforms
  - camera / view matrix
  - perspective and orthographic projection
  - minimal depth-buffered line rendering
- BMP file support
  - saving data
  - loading data
- LVGLDriver: use this library to output data from the lvgl library
- LCDBoard: one-call setup (display + touch, and I2S pins where present) for ESP32(-S3/-P4) boards from the [arduino-audio-tools Audio Boards wiki](https://github.com/pschatzmann/arduino-audio-tools/wiki/Audio-Boards)
  - ESP32-S3 2.8" Display (FBBA0125-002 / ESP32-S3 Hosyond Display)
  - Guition ESP32-S3 4.3" Capacitive Touch Display (JC4827W543C_I)
  - ESP32 Arduino LVGL WiFi&Bluetooth 2.4" LCD (ESP32-2432S028R / ESP32 Cheap Yellow Display)
  - Guition ESP32-P4 4.3" 480x800 Capacitive Touch Display (JC4880P443C_I_W)
- Arduino example sketches

## Overview

TinyGPU is designed as a small in-memory rendering layer that stays independent from any specific display driver. You render into RGB565 memory first and then forward the resulting pixel data to your own hardware-specific output code.

The library covers three main areas:

- 2D drawing and text rendering for compact embedded displays
- sprite-oriented composition and transforms for UI and simple animation
- lightweight 3D wireframe rendering for visualizations and demos


## Documentaion

- [Class Documentation](https://pschatzmann.github.io/TinyGPU/namespacetinygpu.html)
- [Wiki](https://github.com/pschatzmann/TinyGPU/wiki)
- [Examples](examples)


## Installation

For Arduino, you can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyGPU.git
```

