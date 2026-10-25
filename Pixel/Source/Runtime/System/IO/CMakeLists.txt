add_library(PixelIO STATIC FileIO.cpp)
target_include_directories(PixelIO PUBLIC "${PIXEL_SOURCE_DIR}")
target_compile_features(PixelIO PUBLIC cxx_std_20)

# Keep gameplay input in its own target; PixelIO remains a reusable file service.
add_subdirectory(Input)
