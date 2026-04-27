

# Sketch with additional libraries
arduino_sketch(test-compile test-compile.ino LIBRARIES TinyGPU)
target_compile_definitions(test-compile PRIVATE TINYGPU_AUTO_NAMESPACE)