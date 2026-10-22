add_library(PixelActors STATIC Actors.cpp Character.cpp)
target_include_directories(PixelActors PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(PixelActors PUBLIC PixelWorld)
