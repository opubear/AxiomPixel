# Register game-owned shaders before configuring the engine shader compiler.
set(AXIOM_APPLICATION_SHADER_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Pixel2DFullscreen.Vert.slang"
    "${CMAKE_CURRENT_LIST_DIR}/Pixel2DWorldGrid.Frag.slang"
    "${CMAKE_CURRENT_LIST_DIR}/Pixel2DPostProcess.Frag.slang"
    "${CMAKE_CURRENT_LIST_DIR}/Pixel2DCharacter.Frag.slang"
    "${CMAKE_CURRENT_LIST_DIR}/Pixel2DWind.Frag.slang")
