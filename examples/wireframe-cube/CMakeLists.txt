
# find SDL2 library (needed by LCDBoardDesktopSDL)
find_package(SDL2 REQUIRED)

# Sketch with additional libraries
arduino_sketch(wireframe-cube wireframe-cube.ino LIBRARIES TinyGPU SDL2::SDL2)
target_compile_definitions(wireframe-cube PRIVATE TINYGPU_AUTO_NAMESPACE)