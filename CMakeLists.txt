cmake_minimum_required(VERSION 3.14)

if (DEFINED ESP_PLATFORM)

    # ESP-IDF component build. Several headers (TouchDriver.h,
    # DeviceOutput.h, Boards/LCDBoardsESP32.h, ...) include Arduino.h
    # unconditionally, so this only compiles when arduino-esp32 is also
    # present as a component in the same build - see idf_component.yml.
    if(TARGET arduino-esp32::arduino-esp32)
        set(TGPU_REQUIRES arduino-esp32)
    else()
        set(TGPU_REQUIRES "")
    endif()

    idf_component_register(
        INCLUDE_DIRS src
        REQUIRES ${TGPU_REQUIRES}
    )

else()

    project(TinyGPU LANGUAGES CXX)

    # Set C++ standard
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    option(TGPU_BUILD_EXAMPLES "Build Examples" ON)

    # Fetch Arduino-Emulator from GitHub
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

    # Create TinyGPU as a header-only interface library
    add_library(TinyGPU INTERFACE)
    target_link_libraries(TinyGPU INTERFACE arduino_emulator)
    target_include_directories(TinyGPU INTERFACE  ${CMAKE_CURRENT_SOURCE_DIR}/src)

    # Optionally, if you want to build examples:
    if(TGPU_BUILD_EXAMPLES)
    add_subdirectory(examples)
    endif()

endif()
