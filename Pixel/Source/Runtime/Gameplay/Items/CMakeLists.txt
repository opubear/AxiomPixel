add_library(PixelItems STATIC Item.cpp Inventory.cpp)
target_include_directories(PixelItems PUBLIC "${PIXEL_SOURCE_DIR}")
