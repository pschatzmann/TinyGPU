

# Sketch with additional libraries
arduino_sketch(sprite-demo sprite-demo.ino LIBRARIES TinyGPU)
target_compile_definitions(sprite-demo PRIVATE TINYGPU_AUTO_NAMESPACE)