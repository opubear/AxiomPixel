add_library(PixelObservationValues STATIC ../GameAPI/Observations.cpp ../Observation/DomainFactBuffer.cpp)
target_include_directories(PixelObservationValues PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(PixelGameplay STATIC
    GameSession.cpp CommittedWorldView.cpp SessionPublication.cpp Actions/GameActions.cpp FormulaRuntime.cpp FormulaWorldAdapter.cpp
    SessionObservation.cpp ../Observation/ObservationProjector.cpp
    ../Persistence/SessionStore.cpp)
target_link_libraries(PixelGameplay PUBLIC PixelWorld PixelFormulaValidation PixelObservationValues)
