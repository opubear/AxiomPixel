find_package(Threads REQUIRED)
add_library(PixelWorldWill STATIC WorldWill.cpp FakeModel.cpp)
target_link_libraries(PixelWorldWill PUBLIC PixelLLMView PRIVATE Threads::Threads)
