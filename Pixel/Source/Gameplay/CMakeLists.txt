add_library(PixelGameplay STATIC
    GameSession.cpp Actions/GameActions.cpp FormulaRuntime.cpp FormulaWorldAdapter.cpp
    ../Persistence/SessionStore.cpp)
target_link_libraries(PixelGameplay PUBLIC PixelWorld PixelFormulaValidation)
