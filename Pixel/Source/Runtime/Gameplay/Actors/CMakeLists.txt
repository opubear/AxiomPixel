add_library(PixelActors STATIC Actors.cpp Body.cpp)
target_include_directories(PixelActors PUBLIC "${PIXEL_SOURCE_DIR}")
target_link_libraries(PixelActors PUBLIC PixelWorld)
