# Pure CPU world. The environment is part of World, with no runtime/RHI/UI dependency.
set(PIXEL_CHUNK_EDGE "1024" CACHE STRING "World physical chunk edge (power of two, at least 64)")
if(NOT PIXEL_CHUNK_EDGE MATCHES "^[0-9]+$")
    message(FATAL_ERROR "PIXEL_CHUNK_EDGE must be an integer")
endif()
add_library(PixelWorld STATIC
    World.cpp WorldMap.cpp WorldStreamer.cpp WorldBuffers.cpp
    Generation/WorldGenerator.cpp Generation/GenerationNoise.cpp Generation/BiomeTerrain.cpp
    Environment/Environment.cpp Environment/FieldSources.cpp Environment/CellularMaterials.cpp
    Environment/Wind/PixelWindField.cpp
    Environment/Wind/WindSimulation.cpp
    Environment/Wind/WindStrategy.cpp
    Environment/Wind/LocalWindGeometry.cpp
    Environment/Wind/ProceduralWindField.cpp
    Environment/Wind/AdaptiveWindField.cpp
    Environment/Wind/LbmWindField.cpp
    Environment/Wind/WindField.cpp)
target_link_libraries(PixelWorld PUBLIC PixelPerformance PixelIO Axiom::Core)
target_compile_definitions(PixelWorld PUBLIC PIXEL_CHUNK_EDGE=${PIXEL_CHUNK_EDGE})
