add_library(PixelMemory STATIC PageBuffer.cpp)
target_include_directories(PixelMemory PUBLIC "${PROJECT_SOURCE_DIR}/Pixel/Source")
target_compile_features(PixelMemory PUBLIC cxx_std_20)
