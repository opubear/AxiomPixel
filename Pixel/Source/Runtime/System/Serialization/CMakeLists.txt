add_library(PixelSerialization INTERFACE)
target_include_directories(PixelSerialization INTERFACE "${PIXEL_SOURCE_DIR}")
target_compile_features(PixelSerialization INTERFACE cxx_std_20)
