cmake_minimum_required(VERSION 3.23)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PixelFormulaPrototype LANGUAGES CXX)
    include(CTest)
endif()

add_library(PixelFormula STATIC Source/Formula.cpp Source/Parser.cpp Source/Simulator.cpp)
target_include_directories(PixelFormula PUBLIC Include)
target_compile_features(PixelFormula PUBLIC cxx_std_20)
set_target_properties(PixelFormula PROPERTIES CXX_EXTENSIONS OFF)

add_executable(PixelFormulaDemo Source/Main.cpp)
target_link_libraries(PixelFormulaDemo PRIVATE PixelFormula)

# Keep standalone Formula builds wired to the shared test directory.
if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../UnitTest/Formula" "UnitTest/Formula")
    set_target_properties(PixelFormulaTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
