add_library(PixelUI STATIC InventoryUI.cpp InventoryPainter.cpp)
target_link_libraries(PixelUI PUBLIC PixelObservationValues)
