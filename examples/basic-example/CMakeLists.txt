
# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Desktop build
arduino_sketch(basic-example basic-example.ino LIBRARIES TinyGPU SDL2::SDL2)
target_compile_definitions(basic-example PRIVATE TINYGPU_AUTO_NAMESPACE)