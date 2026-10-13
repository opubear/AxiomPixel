# World remains usable without any engine, renderer, window or GPU dependency.
add_library(PixelWorld STATIC
    Source/World/World.cpp
    Source/World/Character.cpp
    Source/World/WorldState.cpp
    Source/World/PixelWindField.cpp
    Source/World/WindSimulation.cpp
    Source/World/WindStrategy.cpp
    Source/World/LocalWindGeometry.cpp
    Source/World/ProceduralWindField.cpp
    Source/World/AdaptiveWindField.cpp
    Source/World/LbmWindField.cpp
    Source/World/WindField.cpp
    Source/World/Environment.cpp)
target_include_directories(PixelWorld PUBLIC Source)
target_compile_features(PixelWorld PUBLIC cxx_std_20)

# The renderer uses CPU-only RHI contracts. The application supplies the real
# backend; the CPU tests supply a mock and the shader lookup implementation.
add_library(PixelRender STATIC
    Source/Camera/Camera2D.cpp
    Source/PixelRender/PixelRender.cpp
    Source/PixelRender/WorldGridFrame.cpp
    Source/PixelRender/WindVisualFrame.cpp
    Source/PixelRender/PixelWorldPresentation.cpp)
target_include_directories(PixelRender PUBLIC Source "${AXIOM_ENGINE_DIR}/Source")
target_link_libraries(PixelRender PUBLIC PixelWorld)

if(AXIOM_BUILD_RUNTIME)
    add_library(PixelGame STATIC Source/PixelGame.cpp)
    target_link_libraries(PixelGame PUBLIC PixelRender AxiomRuntime PRIVATE spdlog ImGui)
    add_executable(Pixel Source/main.cpp)
    target_link_libraries(Pixel PRIVATE PixelGame)
    set_target_properties(Pixel PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_custom_target(run-pixel
        COMMAND "$<TARGET_FILE:Pixel>"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        DEPENDS Pixel USES_TERMINAL)

    if(PIXEL_BUILD_SMOKE)
        add_executable(PixelRuntimeSmoke Tests/Pixel2DRuntimeSmoke.cpp Tests/PixelDebugUISmoke.cpp)
        target_link_libraries(PixelRuntimeSmoke PRIVATE PixelGame ImGui)
        set_target_properties(PixelRuntimeSmoke PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    endif()
endif()

if(BUILD_TESTING)
    add_library(PixelHeaderCompileCheck OBJECT Tests/HeaderCompileCheck.cpp)
    target_link_libraries(PixelHeaderCompileCheck PRIVATE PixelRender)
    add_executable(PixelTests Tests/Pixel2DTests.cpp Tests/PixelWorldTests.cpp Tests/PixelFieldTests.cpp Tests/PixelWindTests.cpp Tests/PixelFieldViewTests.cpp Tests/PixelDirtWindTests.cpp Tests/PixelWindVisualTests.cpp)
    target_link_libraries(PixelTests PRIVATE PixelRender)
    add_test(NAME Pixel.Cpu COMMAND PixelTests)
endif()

if(BUILD_TESTING)
    add_executable(PixelBaselineTests Tests/PixelBaselineTests.cpp)
    target_link_libraries(PixelBaselineTests PRIVATE PixelWorld)
    add_test(NAME Pixel.Baseline COMMAND PixelBaselineTests)
    add_executable(PixelWindStrategyTests Tests/PixelWindStrategyTests.cpp)
    target_link_libraries(PixelWindStrategyTests PRIVATE PixelWorld)
    add_test(NAME Pixel.WindStrategies COMMAND PixelWindStrategyTests)
endif()

# Explicit/offline reference measurements, not part of the fast CTest suite.
add_executable(PixelWindBenchmark EXCLUDE_FROM_ALL Tests/PixelWindBenchmark.cpp)
target_link_libraries(PixelWindBenchmark PRIVATE PixelWorld)

add_executable(PixelWindStrategyBenchmark EXCLUDE_FROM_ALL Tests/PixelWindStrategyBenchmark.cpp)
target_link_libraries(PixelWindStrategyBenchmark PRIVATE PixelWorld)

if(BUILD_TESTING)
    add_executable(PixelCharacterTests Tests/PixelCharacterTests.cpp)
    target_link_libraries(PixelCharacterTests PRIVATE PixelRender)
    add_test(NAME Pixel.Character COMMAND PixelCharacterTests)
endif()

if(BUILD_TESTING)
    add_executable(PixelCameraTests Tests/PixelCameraTests.cpp)
    target_link_libraries(PixelCameraTests PRIVATE PixelRender)
    add_test(NAME Pixel.Camera COMMAND PixelCameraTests)
endif()
