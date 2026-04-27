

# Sketch with additional libraries
arduino_sketch(wireframe-cube wireframe-cube.ino LIBRARIES TinyGPU)
target_compile_definitions(wireframe-cube PRIVATE TINYGPU_AUTO_NAMESPACE)