
# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Desktop build
arduino_sketch(font-test font-test.ino LIBRARIES TinyGPU SDL2::SDL2)
target_compile_definitions(font-test PRIVATE TINYGPU_AUTO_NAMESPACE)
