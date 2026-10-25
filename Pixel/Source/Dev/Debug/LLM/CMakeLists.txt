find_package(Threads REQUIRED)
add_library(PixelFakeModel STATIC FakeModel.cpp)
target_link_libraries(PixelFakeModel PUBLIC PixelLLM PRIVATE Threads::Threads)
