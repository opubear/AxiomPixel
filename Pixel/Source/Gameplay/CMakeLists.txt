add_library(PixelObservationValues STATIC ../GameAPI/Actions.cpp ../GameAPI/Observations.cpp ../Observation/DomainFactBuffer.cpp ../Knowledge/Knowledge.cpp ../Authority/Authority.cpp)
target_include_directories(PixelObservationValues PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(PixelObservationValues PUBLIC PixelDevices)

add_library(PixelGameplay STATIC
    GameSession.cpp CommittedWorldView.cpp SessionPublication.cpp Actions/GameActions.cpp Actions/Mining.cpp
    Construction/DeviceActions.cpp Construction/DeviceRuntime.cpp
    Home/ForceGarden.cpp SessionAuthority.cpp SessionWorldWill.cpp
    FormulaBridge/FormulaBridge.cpp FormulaBridge/FormulaWorldAdapter.cpp FormulaBridge/ProcessHost.cpp
    SessionObservation.cpp SessionGameplayObservation.cpp SessionPersistence.cpp ../Observation/ObservationProjector.cpp
    ../Persistence/SessionStore.cpp ../Persistence/GameplaySaveData.cpp
    ../Persistence/KnowledgeCodec.cpp ../Persistence/WorldWillCodec.cpp)
target_link_libraries(PixelGameplay PUBLIC PixelActors PixelDevices PixelItems PixelFormulaValidation PixelFormulaProcess PixelObservationValues)
