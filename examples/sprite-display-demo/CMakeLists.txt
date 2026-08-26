
# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Desktop build
arduino_sketch(sprite-display-demo sprite-display-demo.ino LIBRARIES TinyGPU SDL2::SDL2)
target_compile_definitions(sprite-display-demo PRIVATE TINYGPU_AUTO_NAMESPACE)
