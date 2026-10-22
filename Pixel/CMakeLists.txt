# The formula language also supports standalone configuration with -S Pixel/Formula.
add_subdirectory(Formula)
set(PIXEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source")
add_subdirectory(Source)
add_executable(PixelObservationDemo Tools/ObservationDemo.cpp)
target_link_libraries(PixelObservationDemo PRIVATE PixelGameplay PixelLLMView)
if(BUILD_TESTING)
    add_test(NAME Pixel.ObservationDemo COMMAND PixelObservationDemo)
endif()
