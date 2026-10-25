add_library(PixelLLMView STATIC ViewService.cpp QueryProjection.cpp JsonAdapter.cpp DerivedMemory.cpp)
target_link_libraries(PixelLLMView PUBLIC PixelObservationValues)
target_include_directories(PixelLLMView PUBLIC "${AXIOM_ENGINE_DIR}/ThirdParty/nlohmannJson/single_include")
