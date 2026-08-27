# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Fetch TinyJPEG from GitHub, matching the arduino_emulator/lvgl
# FetchContent pattern in the top-level/lvgl-example CMakeLists.txt.
# TinyJPEG is header-only (INTERFACE target), so this just adds it to the
# include path - no sources to compile.
if(NOT TARGET TinyJPEG)
  include(FetchContent)
  set(FETCHCONTENT_QUIET OFF)
  FetchContent_Declare(
    TinyJPEG
    GIT_REPOSITORY https://github.com/pschatzmann/TinyJPEG.git
    GIT_TAG        main
  )
  FetchContent_MakeAvailable(TinyJPEG)
endif()

# Desktop build
arduino_sketch(jpeg-parser-demo jpeg-parser-demo.ino LIBRARIES TinyGPU SDL2::SDL2 TinyJPEG)
target_compile_definitions(jpeg-parser-demo PRIVATE TINYGPU_AUTO_NAMESPACE)
