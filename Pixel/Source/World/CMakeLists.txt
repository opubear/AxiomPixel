# Pure CPU world. The environment is part of World, with no runtime/RHI/UI dependency.
add_library(PixelWorld STATIC
    World.cpp Character.cpp WorldState.cpp
    Environment/Environment.cpp
    Environment/Wind/PixelWindField.cpp
    Environment/Wind/WindSimulation.cpp
    Environment/Wind/WindStrategy.cpp
    Environment/Wind/LocalWindGeometry.cpp
    Environment/Wind/ProceduralWindField.cpp
    Environment/Wind/AdaptiveWindField.cpp
    Environment/Wind/LbmWindField.cpp
    Environment/Wind/WindField.cpp)
target_link_libraries(PixelWorld PUBLIC PixelPerformance)
