add_library(PixelLLM STATIC LLM.cpp DecisionAPI.cpp PseudoLLM.cpp)
target_link_libraries(PixelLLM PUBLIC PixelObservationValues)

# Provider wire serialization has no I/O and uses the same DecisionAPI.
add_library(PixelModelProtocol STATIC Services/ProposalProtocol.cpp Services/ServiceLLM.cpp)
target_link_libraries(PixelModelProtocol PUBLIC PixelLLM PixelModelService)
target_include_directories(PixelModelProtocol PRIVATE "${AXIOM_ENGINE_DIR}/ThirdParty/nlohmannJson/single_include")
