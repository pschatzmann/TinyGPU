#pragma once
#include "TinyGPU/Drivers/DisplayDriverSPI.h"
// QSPI now compiles on every platform: ESP32 gets QSPIBusESP32 (hardware/
// DMA), everything else (RP2040, STM32, ...) gets QSPIBusBitBang (portable
// software fallback) - see DisplayDriverQSPI.h.
#include "TinyGPU/Drivers/DisplayDriverQSPI.h"
// DSI panel subclasses (ST7701Driver, ...) are individually guarded by
// TINYGPU_HAS_ESP_LCD_DSI/TINYGPU_HAS_STM32_DSI and no-op away on
// platforms/parts without a DSI host (including all of RP2040, which has
// no MIPI-DSI peripheral at all) - see DisplayDriverDSI.h.
#include "TinyGPU/Drivers/DisplayDriverDSI.h"

// #include "TinyGPU/Drivers/DisplayDriverSDL.h"
// #include "TinyGPU/Drivers/DisplayDriverTFTeSPI.h"
