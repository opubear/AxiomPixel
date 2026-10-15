# The formula language also supports standalone configuration with -S Pixel/Formula.
add_subdirectory(Formula)
set(PIXEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Source")
add_subdirectory(Source)
if(BUILD_TESTING OR PIXEL_BUILD_SMOKE)
    add_subdirectory(Tests)
endif()
add_subdirectory(Benchmarks)
