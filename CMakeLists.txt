cmake_minimum_required(VERSION 3.14)

if (DEFINED ESP_PLATFORM)

    # ESP-IDF component build. Some headers (Input/TouchDriverArduino.h,
    # Boards/LCDBoardsESP32.h, ...) call into the Arduino API
    # (delay/pinMode/digitalWrite/SPIClass/TwoWire, ...) via
    # TinyGPU/Emulation/EmulationIDF.h. That header uses the real
    # arduino-esp32 component when it's present in the build, and falls
    # back to a small ESP-IDF-native emulation (driver/gpio.h,
    # driver/spi_master.h, driver/i2c.h, esp_timer) otherwise - see
    # EmulationIDF.h - so arduino-esp32 is picked up automatically when
    # available but is no longer a hard requirement. driver/esp_timer are
    # REQUIRES unconditionally since the emulation fallback always needs
    # their headers on the include path, whether or not it ends up being
    # compiled for a given TU.
    if(TARGET arduino-esp32::arduino-esp32)
        set(TGPU_REQUIRES arduino-esp32)
    else()
        set(TGPU_REQUIRES "")
    endif()

    idf_component_register(
        INCLUDE_DIRS src
        REQUIRES ${TGPU_REQUIRES} driver esp_timer
    )

else()

    # C is needed alongside CXX because lvgl (pulled in by the desktop build
    # of lvgl-example, if that sibling Arduino library is present) is a C
    # library - see examples/lvgl-example/CMakeLists.txt.
    project(TinyGPU LANGUAGES C CXX)

    # Set C++ standard
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    option(TGPU_BUILD_EXAMPLES "Build Examples" ON)

    # Create TinyGPU as a header-only interface library. No Arduino-Emulator
    # dependency here: TinyGPU's own headers no longer require Arduino.h to
    # compile on desktop (TouchDriver.h dispatches to TouchDriverSDL.h/
    # TouchDriverCommon.h off ESP32, DeviceOutput.h has no Arduino
    # dependency of its own) - only the *examples* are actual Arduino
    # sketches (setup()/loop(), Serial, ...) that need the emulator's
    # runtime, so it's fetched below only when they're built.
    add_library(TinyGPU INTERFACE)
    target_include_directories(TinyGPU INTERFACE  ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Optionally, if you want to build examples:
    if(TGPU_BUILD_EXAMPLES)
        # Fetch Arduino-Emulator from GitHub - every example is a .ino
        # sketch (arduino_sketch(), from Arduino.cmake below) that needs
        # its setup()/loop() wrapper and Serial/millis/delay/... runtime,
        # regardless of what TinyGPU.h itself does or doesn't need.
        include(FetchContent)
        set(FETCHCONTENT_QUIET OFF)
        FetchContent_Declare(
          arduino_emulator
          GIT_REPOSITORY https://github.com/pschatzmann/Arduino-Emulator.git
          GIT_TAG        main
          CMAKE_ARGS -DUSE_RPI=OFF
          GIT_CLONE_TIMEOUT 600  # Timeout in seconds (e.g., 5 minutes)
        )
        FetchContent_MakeAvailable(arduino_emulator)

        add_subdirectory(examples)
    endif()

endif()
