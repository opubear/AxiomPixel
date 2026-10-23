add_library(PixelItems STATIC Item.cpp Inventory.cpp)
target_include_directories(PixelItems PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
