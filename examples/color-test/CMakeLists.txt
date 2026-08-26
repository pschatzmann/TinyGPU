
# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Desktop build
arduino_sketch(color-test color-test.ino LIBRARIES TinyGPU SDL2::SDL2)
target_compile_definitions(color-test PRIVATE TINYGPU_AUTO_NAMESPACE)
