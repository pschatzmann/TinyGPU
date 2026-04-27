
# find SDL2 library
find_package(SDL2 REQUIRED)

# Sketch with additional libraries
arduino_sketch(desktop desktop.ino LIBRARIES TinyGPU SDL2::SDL2)
target_compile_definitions(desktop PRIVATE TINYGPU_AUTO_NAMESPACE)