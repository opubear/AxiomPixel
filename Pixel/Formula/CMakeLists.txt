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

if(BUILD_TESTING)
    add_executable(PixelFormulaTests Tests/FormulaTests.cpp)
    target_link_libraries(PixelFormulaTests PRIVATE PixelFormula)
    add_test(NAME Pixel.Formula COMMAND PixelFormulaTests)
    add_test(NAME Pixel.Formula.Demo COMMAND PixelFormulaDemo
        "${CMAKE_CURRENT_SOURCE_DIR}/Examples/impact_burst.formula" --ticks 30 --hit-at 6)
    set_tests_properties(Pixel.Formula.Demo PROPERTIES PASS_REGULAR_EXPRESSION "wind radius=8")
endif()
