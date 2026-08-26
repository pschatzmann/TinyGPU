
# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Desktop build
arduino_sketch(bouncing-ball bouncing-ball.ino LIBRARIES TinyGPU SDL2::SDL2)
target_compile_definitions(bouncing-ball PRIVATE TINYGPU_AUTO_NAMESPACE)
