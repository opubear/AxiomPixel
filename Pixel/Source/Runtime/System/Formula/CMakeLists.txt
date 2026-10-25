cmake_minimum_required(VERSION 3.23)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(PixelFormulaProcess LANGUAGES CXX)
    include(CTest)
endif()

# Process v1 is a standard-library-only language/runtime boundary.
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

# Keep standalone Formula builds wired to the shared test directory.
if(BUILD_TESTING AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../../../UnitTest/Formula" "UnitTest/Formula")
endif()
