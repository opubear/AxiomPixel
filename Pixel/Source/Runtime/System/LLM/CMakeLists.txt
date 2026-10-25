add_library(PixelModelService INTERFACE)
target_include_directories(PixelModelService INTERFACE "${PIXEL_SOURCE_DIR}")
target_compile_features(PixelModelService INTERFACE cxx_std_20)
