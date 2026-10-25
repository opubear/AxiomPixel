cmake_minimum_required(VERSION 3.23)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PixelFormulaPrototype LANGUAGES CXX)
    include(CTest)
endif()

add_library(PixelFormula STATIC Source/Formula.cpp Source/Parser.cpp Source/Simulator.cpp)
target_include_directories(PixelFormula PUBLIC Include)
target_compile_features(PixelFormula PUBLIC cxx_std_20)
set_target_properties(PixelFormula PROPERTIES CXX_EXTENSIONS OFF)

# Process v1 is a separate, standard-library-only language/runtime boundary.
add_library(PixelFormulaProcess STATIC
    Source/Language/ProcessArtifact.cpp
    Source/Language/ProcessCompiler.cpp
    Source/Language/ProcessEditing.cpp
    Source/Runtime/ProcessInstance.cpp)
target_include_directories(PixelFormulaProcess PUBLIC Include)
target_compile_features(PixelFormulaProcess PUBLIC cxx_std_20)
set_target_properties(PixelFormulaProcess PROPERTIES CXX_EXTENSIONS OFF)

add_library(PixelFormulaFakeHost STATIC Tools/ProcessFakeHost.cpp)
target_include_directories(PixelFormulaFakeHost PUBLIC Tools)
target_link_libraries(PixelFormulaFakeHost PUBLIC PixelFormulaProcess)
add_executable(PixelFormulaProcessDemo Tools/ProcessMain.cpp)
target_link_libraries(PixelFormulaProcessDemo PRIVATE PixelFormulaFakeHost)

option(PIXEL_FORMULA_BUILD_SEMANTICS_EXPERIMENT "Build the isolated M3 language comparison" OFF)
if(PIXEL_FORMULA_BUILD_SEMANTICS_EXPERIMENT)
    add_subdirectory(Experiments/Semantics)
endif()

add_executable(PixelFormulaDemo Source/Main.cpp)
target_link_libraries(PixelFormulaDemo PRIVATE PixelFormula)

# Keep standalone Formula builds wired to the shared test directory.
if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../../UnitTest/Formula" "UnitTest/Formula")
    set_target_properties(PixelFormulaTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
