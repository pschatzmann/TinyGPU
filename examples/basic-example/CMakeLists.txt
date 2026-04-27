

# Desktop build
arduino_sketch(basic-example basic-example.ino LIBRARIES TinyGPU)
target_compile_definitions(basic-example PRIVATE TINYGPU_AUTO_NAMESPACE)