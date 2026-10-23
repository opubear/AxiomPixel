add_library(PixelLLM STATIC LLM.cpp DecisionAPI.cpp PseudoLLM.cpp)
target_link_libraries(PixelLLM PUBLIC PixelObservationValues)

# Provider wire serialization has no I/O and uses the same DecisionAPI.
add_library(PixelModelProtocol STATIC Services/ProposalProtocol.cpp)
target_link_libraries(PixelModelProtocol PUBLIC PixelLLM)
target_include_directories(PixelModelProtocol PRIVATE "${AXIOM_ENGINE_DIR}/ThirdParty/nlohmannJson/single_include")

if(BUILD_TESTING OR PIXEL_BUILD_SMOKE)
    find_package(Threads REQUIRED)
    add_library(PixelFakeModel STATIC Testing/FakeModel.cpp)
    target_link_libraries(PixelFakeModel PUBLIC PixelLLM PRIVATE Threads::Threads)
endif()
