
# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Fetch lvgl from GitHub, matching the arduino_emulator FetchContent pattern
# in the top-level CMakeLists.txt. lv_conf.h in this directory (a vendored
# copy of lv_conf_template.h with `#if 1` enabled - see that file's own
# header comment) configures it; LV_BUILD_CONF_DIR points lvgl's own build
# script at this directory to find it.
if(NOT TARGET lvgl)
  include(FetchContent)
  set(FETCHCONTENT_QUIET OFF)
  FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        v9.5.0
    GIT_SHALLOW    TRUE
  )
  set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "" FORCE)
  set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(lvgl)
endif()

# Desktop build
arduino_sketch(lvgl-example lvgl-example.ino LIBRARIES TinyGPU SDL2::SDL2 lvgl)
target_compile_definitions(lvgl-example PRIVATE TINYGPU_AUTO_NAMESPACE)
