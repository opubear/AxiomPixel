add_library(PixelUI STATIC InventoryUI.cpp InventoryPainter.cpp CanvasPainter.cpp GameMenuUI.cpp)
target_link_libraries(PixelUI PUBLIC PixelObservationValues PixelWorld)
