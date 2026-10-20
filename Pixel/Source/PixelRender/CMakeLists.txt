# CPU presentation and camera policy can be exercised without a renderer or GPU.
add_library(PixelPresentation STATIC
    ../Camera/Camera2D.cpp ../Camera/GameCamera.cpp
    WorldGridFrame.cpp WindVisualFrame.cpp FormulaFrame.cpp PixelWorldPresentation.cpp GamePresentation.cpp BackgroundView.cpp)
target_link_libraries(PixelPresentation PUBLIC PixelGameplay PixelAssets Axiom::Core)

# Runtime supplies the real RHI backend; CPU tests supply mocks and shader lookup.
add_library(PixelRender STATIC PixelRender.cpp)
target_link_libraries(PixelRender PUBLIC PixelPresentation)
