add_library(PixelDevices STATIC Devices.cpp)
target_include_directories(PixelDevices PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(PixelDevices PUBLIC PixelItems)
