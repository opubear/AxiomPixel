add_subdirectory(Actors)
add_subdirectory(Items)
add_subdirectory(Devices)

add_library(PixelObservationValues STATIC GameAPI/Actions.cpp GameAPI/Observations.cpp
    Observation/DomainFactBuffer.cpp Knowledge/Knowledge.cpp Authority/Authority.cpp)
target_include_directories(PixelObservationValues PUBLIC "${PIXEL_SOURCE_DIR}")
target_link_libraries(PixelObservationValues PUBLIC PixelDevices)

add_library(PixelFormulaBridge STATIC Actions/Mining.cpp FormulaBridge/FormulaBridge.cpp
    FormulaBridge/FormulaWorldAdapter.cpp FormulaBridge/ProcessHost.cpp)
target_link_libraries(PixelFormulaBridge PUBLIC PixelWorld PixelItems PixelActors
    PixelFormulaProcess PixelObservationValues PixelSerialization)

add_library(PixelGameplay STATIC
    GameSession/GameSession.cpp CommittedWorldView.cpp GameSession/SessionPublication.cpp Actions/GameActions.cpp
    Construction/DeviceActions.cpp Construction/DeviceRuntime.cpp
    GameSession/SessionAuthority.cpp GameSession/SessionWorldWill.cpp
    GameSession/SessionObservation.cpp GameSession/SessionGameplayObservation.cpp GameSession/SessionPersistence.cpp
    Observation/ObservationProjector.cpp
    Persistence/SessionStore.cpp Persistence/GameplaySaveData.cpp
    Persistence/KnowledgeCodec.cpp Persistence/WorldWillCodec.cpp)
target_link_libraries(PixelGameplay PUBLIC PixelActors PixelFormulaBridge PixelGarden PixelIO)

add_subdirectory(UI)
add_subdirectory(Camera)
add_subdirectory(LLMView)
add_subdirectory(LLM)
add_subdirectory(WorldWill)
if(AXIOM_BUILD_RUNTIME)
    # Composition root: assembles gameplay, renderer, system providers and dev UI.
    add_subdirectory(Application)
endif()
