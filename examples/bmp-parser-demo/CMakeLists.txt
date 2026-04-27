

# Desktop build
arduino_sketch(bmp-parser-demo bmp-parser-demo.ino LIBRARIES TinyGPU)
target_compile_definitions(bmp-parser-demo PRIVATE TINYGPU_AUTO_NAMESPACE)