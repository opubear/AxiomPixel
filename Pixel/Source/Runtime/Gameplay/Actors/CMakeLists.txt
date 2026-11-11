add_library(PixelActors STATIC Actor.cpp Actors.cpp Body.cpp BodyComponent.cpp FormulaComponent.cpp MonsterCatalog.cpp ../Ecology/BiomePopulation.cpp MonsterComponent.cpp)
target_include_directories(PixelActors PUBLIC "${PIXEL_SOURCE_DIR}")
target_link_libraries(PixelActors PUBLIC PixelWorld)
target_include_directories(PixelActors PRIVATE "${AXIOM_ENGINE_DIR}/ThirdParty/nlohmannJson/single_include")
