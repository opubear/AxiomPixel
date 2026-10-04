cmake_minimum_required(VERSION 3.23)
project(AxiomPixel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type")
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")

include(CTest)
option(AXIOM_BUILD_RUNTIME "Build the engine runtime and Pixel game" ON)
option(AXIOM_BUILD_UNIT_TESTS "Build the engine CPU regression tests" ${BUILD_TESTING})
option(AXIOM_BUILD_FEATURE_SHOW "Build the engine's C++ feature sample" OFF)
option(PIXEL_BUILD_SMOKE "Build the opt-in Pixel GPU smoke executable" OFF)
if(PIXEL_BUILD_SMOKE AND NOT AXIOM_BUILD_RUNTIME)
    message(FATAL_ERROR "PIXEL_BUILD_SMOKE requires AXIOM_BUILD_RUNTIME")
endif()

set(AXIOM_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Axiom")
if(NOT EXISTS "${AXIOM_ENGINE_DIR}/Source/Render/RenderSetup.h")
    message(FATAL_ERROR "Axiom/ must contain the PixelMain engine checkout; see README.md")
endif()

set(AXIOM_APPLICATION_SHADER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Pixel/Shader/Pixel2DFullscreen.Vert.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/Pixel/Shader/Pixel2DWorldGrid.Frag.slang"
    "${CMAKE_CURRENT_SOURCE_DIR}/Pixel/Shader/Pixel2DPostProcess.Frag.slang")
add_subdirectory(Axiom)
add_subdirectory(Pixel)
