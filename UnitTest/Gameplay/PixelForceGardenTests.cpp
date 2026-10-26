#include "../Support/TestAccess.h"
#include "Dev/Debug/Garden/ForceGardenProgram.h"
#include "Runtime/Gameplay/Persistence/GameplaySaveData.h"
#include "Runtime/Simulation/World/Environment/FieldResponse.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace {
using namespace pixel;
using namespace pixel::items;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
std::string ReadText(const std::filesystem::path &path) {
    std::ifstream input(path);
    input.exceptions(std::ios::failbit | std::ios::badbit);
    std::ostringstream content;
    content << input.rdbuf();
    return content.str();
}
uint32_t Quantity(const ContainerSnapshot &container, ItemId item) {
    const auto found = std::find_if(container.Stacks.begin(), container.Stacks.end(), [&](const auto &stack) {
        return stack.Item == item;
    });
    return found == container.Stacks.end() ? 0 : found->Quantity;
}

struct SaveRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-force-garden-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::optional<std::string> Previous;
    std::optional<std::string> PreviousWindow;
    SaveRoot() {
        if (const auto *old = std::getenv("PIXEL_SAVE_ROOT")) Previous = old;
        if (const auto *old = std::getenv("PIXEL_SIMULATION_WINDOW")) PreviousWindow = old;
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", Path.string().c_str());
        _putenv_s("PIXEL_SIMULATION_WINDOW", "256");
#else
        setenv("PIXEL_SAVE_ROOT", Path.string().c_str(), 1);
        setenv("PIXEL_SIMULATION_WINDOW", "256", 1);
#endif
    }
    ~SaveRoot() {
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", Previous ? Previous->c_str() : "");
        _putenv_s("PIXEL_SIMULATION_WINDOW", PreviousWindow ? PreviousWindow->c_str() : "");
#else
        if (Previous) setenv("PIXEL_SAVE_ROOT", Previous->c_str(), 1); else unsetenv("PIXEL_SAVE_ROOT");
        if (PreviousWindow) setenv("PIXEL_SIMULATION_WINDOW", PreviousWindow->c_str(), 1); else unsetenv("PIXEL_SIMULATION_WINDOW");
#endif
        std::filesystem::remove_all(Path);
    }
};

void Step(GameSession &session) {
    session.BeginFrame();
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "single step rejected");
    session.Tick(1.f / 60.f, {});
}

api::GameplayObservation Observe(GameSession &session) {
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, session.GetWorld().GetSimulationBounds());
    return view->Gameplay();
}

ForceGardenObservation Garden(GameSession &session) {
    const auto observation = Observe(session);
    Check(observation.Garden.has_value(), "garden observation missing");
    return *observation.Garden;
}

void CreateGarden(GameSession &session) {
    const auto result = session.Control(NativeCaller::Driver, api::CreateForceGarden{});
    Check(result.Succeeded(), "force garden scene creation failed");
    Check(Garden(session).State.SurfaceY > 0, "garden surface was not initialized");
}

void SetHomeForce(GameSession &session, float acceleration) {
    const auto state = Garden(session).State;
    testing::SessionAccess::World(session).SetBaseForce(GardenHomeX, state.SurfaceY - 1, {acceleration, 0});
    testing::SessionAccess::PublishFixtureChanges(session);
}

void VerifyBedsAndForceThresholds() {
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    generation.Seed = 0x4d3438;
    GameSession session(false, generation);
    CreateGarden(session);

    // The natural crystal owns only the wild bed. Its sampled disk must not
    // become a home bed source when the player field is absent.
    Step(session);
    auto state = Garden(session);
    Check(state.Wild.Sampled && state.Wild.HorizontalAcceleration >= GardenMinimumForce
        && state.Home.HorizontalAcceleration == 0 && state.State.WildGrowth == 1 && state.State.HomeGrowth == 0,
        "natural crystal crossed into the home bed");

    SetHomeForce(session, 0);
    for (int i = 0; i < 4; ++i) Step(session);
    Check(Garden(session).State.HomeGrowth == 0, "home bed grew without horizontal force");
    SetHomeForce(session, GardenMinimumForce - 1);
    for (int i = 0; i < 4; ++i) Step(session);
    Check(Garden(session).State.HomeGrowth == 0, "sub-threshold force grew the home bed");
    SetHomeForce(session, GardenMaximumForce + 1);
    for (int i = 0; i < 4; ++i) Step(session);
    state = Garden(session);
    Check(state.Home.Condition == GardenCondition::ExcessForce && state.State.HomeGrowth == 0,
        "excess force was accepted by an immature bed");
    SetHomeForce(session, -GardenMinimumForce);
    for (int i = 0; i < 4; ++i) Step(session);
    state = Garden(session);
    Check(state.Home.Condition == GardenCondition::NeedsForce && state.State.HomeGrowth == 0,
        "opposite force was accepted by an immature bed");
    SetHomeForce(session, GardenMinimumForce);
    for (int i = 0; i < GardenMatureSteps - 1; ++i) Step(session);
    state = Garden(session);
    Check(state.State.HomeGrowth == GardenMatureSteps - 1, "minimum positive force missed a successful growth step");

    SetHomeForce(session, GardenMaximumForce);
    Step(session);
    state = Garden(session);
    Check(state.State.HomeGrowth == GardenMatureSteps && state.Home.Condition == GardenCondition::Mature,
        "maximum positive force did not complete the final growth step");

    auto &world = testing::SessionAccess::World(session);
    const auto y = state.State.SurfaceY;
    world.SetCell(GardenHomeX, y - 1, {CellTypeId::Stone});
    testing::SessionAccess::PublishFixtureChanges(session);
    Step(session);
    state = Garden(session);
    Check(state.Home.Condition == GardenCondition::Obstructed && state.State.HomeGrowth == GardenMatureSteps,
        "blocked flower sample was not reported as obstructed");
    world.SetCell(GardenHomeX, y - 1, {CellTypeId::Air});
    world.SetCell(GardenHomeX, y, {CellTypeId::Air});
    testing::SessionAccess::PublishFixtureChanges(session);
    Step(session);
    Check(Garden(session).Home.Condition == GardenCondition::Obstructed,
        "unsupported flower sample grew without solid support");
}

void VerifyDeviceSupplyAndFuel() {
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    generation.Seed = 0x4d3438;
    GameSession session(false, generation);
    CreateGarden(session);
    const auto surface = Garden(session).State.SurfaceY;
    auto &world = testing::SessionAccess::World(session);

    // The scene deliberately places a real vein. Mine its cells through the
    // public player action so device construction cannot bypass inventory.
    for (int x = 516; x <= 521; ++x) {
        const auto before = world.GetCell(x, surface + 2);
        Check(before.Type != CellTypeId::Air, "prepared stone/copper vein missing");
        const auto mined = session.Execute(NativeCaller::Driver,
            api::MineRegion{{x, surface + 2}, uint32_t(before.Type)});
        Check(mined.Applied(), "player could not mine prepared construction vein");
    }
    for (int x = 522; x <= 537; ++x) {
        const auto before = world.GetCell(x, surface + 2);
        Check(before.Type == CellTypeId::CoalOre, "prepared coal seam missing");
        Check(session.Execute(NativeCaller::Driver,
            api::MineRegion{{x, surface + 2}, uint32_t(CellTypeId::CoalOre)}).Applied(), "player could not mine coal");
    }
    const auto inventoryAfterVein = session.GetInventory().Inspect(session.PlayerInventory());
    Check(!session.Execute(NativeCaller::Driver,
        api::MineRegion{{522, surface + 2}, uint32_t(CellTypeId::CoalOre)}).Applied()
        && session.GetInventory().Inspect(session.PlayerInventory()) == inventoryAfterVein,
        "mined coal was collected twice from an already empty seam cell");
    const auto coalBeforeSupply = Quantity(session.GetInventory().Inspect(session.PlayerInventory()), ItemId::Coal);

    const auto placed = session.Execute(NativeCaller::Driver, api::PlaceDevice{{GardenDeviceX, surface - 2}});
    Check(placed.Applied(), "mined materials did not pay device cost");
    auto device = std::get<api::DeviceChanged>(placed.Result).Device;
    Check(session.Execute(NativeCaller::Driver, api::SupplyDevice{device.Ref, 1}).Applied(), "mined coal could not fuel device");
    device = session.GetDevices().Find(device.Ref.Id)->Value;
    Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{device.Ref, true}).Applied(), "device enable failed");
    Check(bool(session.ApplyProcess(NativeCaller::Driver, ForceGardenProgram())), "player force garden program was rejected");

    const auto fuel = session.GetInventory().Inspect(session.GetDevices().Find(device.Ref.Id)->FuelContainer);
    Check(coalBeforeSupply == 16
        && Quantity(session.GetInventory().Inspect(session.PlayerInventory()), ItemId::Coal) == coalBeforeSupply - 1
        && Quantity(fuel, ItemId::Coal) == 1,
        "mined seam coal was not transferred into the device fuel inventory");

    for (int i = 0; i < 180; ++i) Step(session);
    const auto state = Garden(session);
    const auto current = session.GetDevices().Find(device.Ref.Id)->Value;
    Check(state.State.HomeGrowth > 0 && current.Cycles > 0 && current.Fuel == 0,
        "supplied field device did not grow the home bed or consume its coal");
    const auto coalAfterRun = Quantity(session.GetInventory().Inspect(session.PlayerInventory()), ItemId::Coal);
    Check(coalBeforeSupply == coalAfterRun + current.Fuel + 1,
        "device fuel was duplicated, lost, or charged more than once");
}

void VerifyPauseAndPendingCommit() {
    SaveRoot root;
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    GameSession session(true, generation);
    CreateGarden(session);
    SetHomeForce(session, GardenMinimumForce);
    Check(session.Control(NativeCaller::Driver, api::SetPaused{true}).Succeeded(), "pause failed");
    const auto paused = Garden(session).State.HomeGrowth;
    session.Tick(1.f, {});
    Check(Garden(session).State.HomeGrowth == paused, "paused clock advanced garden growth");
    Check(session.Control(NativeCaller::Driver, api::SetPaused{false}).Succeeded(), "unpause failed");

    auto &world = testing::SessionAccess::World(session);
    WindStrategySettings wind;
    wind.Kind = WindStrategyKind::Procedural;
    world.ConfigureWindDomain(256, wind);
    const auto motionY = Garden(session).State.SurfaceY - 8;
    for (const auto x : {600, 602, 604, 606}) world.SetCell(x, motionY, {CellTypeId::Dirt, {0, 1}});
    world.SetBaseForce(600, motionY, {0, 3600});
    world.SetBaseForce(602, motionY, {3600, 0});
    world.SetBaseForce(604, motionY, {-3600, 0});
    world.SetBaseForce(606, motionY, {0, -3600});
    testing::WorldAccess::Map(world).Wake(world.GetSimulationBounds());
    testing::SessionAccess::PublishFixtureChanges(session);
    Check(FieldResponse::MovementDirection(world.GetFieldVelocity(600, motionY)) != ForceDirection{0, 0}
        && FieldResponse::MovementDirection(world.GetFieldVelocity(602, motionY)) != ForceDirection{0, 0}
        && FieldResponse::MovementDirection(world.GetFieldVelocity(600, motionY))
            != FieldResponse::MovementDirection(world.GetFieldVelocity(602, motionY)),
        "pending fixture did not create distinct moving material directions");
    world.WaitForStreaming();
    session.Tick(0, {});
    const auto *stream = world.GetStreamer();
    Check(bool(stream), "pending fixture did not enable streaming");
    // Publish the committed baseline while capacity is available. A new view
    // is allowed to remain empty if first requested after the budget is pinned.
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, world.GetSimulationBounds());
    Check(view->IsValid() && view->Gameplay().Garden.has_value(), "pending fixture needs a published garden baseline");
    world.WaitForStreaming();
    const auto activeBounds = world.GetSimulationBounds();
    for (const auto coordinate : testing::WorldAccess::Map(world).ChunkCoordinates())
        if (!WorldRect{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge,
                       storage::ChunkEdge, storage::ChunkEdge}.Intersects(activeBounds))
            testing::WorldAccess::Map(world).Remove(coordinate);
    Check(testing::WorldAccess::Map(world).ChunkCount() > 0, "pending fixture removed every active chunk");
    testing::SessionAccess::PublishFixtureChanges(session);
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "pending fixture step request failed");
    const auto stamp = session.CurrentCommit();
    testing::WorldAccess::SaturateBudget(world);
    session.Tick(1.f / 60.f, {});
    if (!world.GetEnvironment().IsWaitingForBudget() || session.CurrentCommit() != stamp)
        throw std::runtime_error("environment did not enter a real pending budget step (waiting="
            + std::to_string(world.GetEnvironment().IsWaitingForBudget()) + ", active_regions="
            + std::to_string(world.GetEnvironment().LastActiveRegions()) + ", visited="
            + std::to_string(world.GetEnvironment().LastVisitedCells()) + ", retained="
            + std::to_string(stream->RetainedBytes()) + ", budget="
            + std::to_string(stream->Settings().BudgetBytes) + ", publication="
            + std::to_string(world.PublicationBytesFor(nullptr)) + ", stamp="
            + std::to_string(stamp.WorldEpoch) + ":" + std::to_string(stamp.Tick) + ":" + std::to_string(stamp.Revision)
            + ", current=" + std::to_string(session.CurrentCommit().WorldEpoch) + ":"
            + std::to_string(session.CurrentCommit().Tick) + ":" + std::to_string(session.CurrentCommit().Revision) + ")");
    Check(view->Gameplay().Garden->State.HomeGrowth == paused, "pending step changed committed garden growth");
    const auto saveDirectory = session.GetSaveDirectory();
    const auto checkpointBefore = ReadText(saveDirectory / "checkpoint.current");
    const auto deferredReset = session.Control(NativeCaller::Driver, api::CreateForceGarden{});
    Check(deferredReset.Status == api::ControlStatus::Deferred && session.CurrentCommit() == stamp
        && session.GetSaveDirectory() == saveDirectory
        && ReadText(saveDirectory / "checkpoint.current") == checkpointBefore,
        "garden replacement during a pending step changed the epoch or checkpoint");
    session.Tick(.1f, {});
    Check(world.GetEnvironment().IsWaitingForBudget() && view->Gameplay().Garden->State.HomeGrowth == paused,
        "budget retry advanced pending garden growth");
    world.SetPublicationBytes(0);
    session.Tick(.1f, {});
    Check(Garden(session).State.HomeGrowth == paused + 1, "committed step did not add exactly one growth tick");
}

void VerifyGardenCodecAndSmallWindowRejection() {
    GameplaySaveData state;
    state.PlayerBag = items::ContainerId{1};
    state.Programs = "{}";
    state.Garden = {true, false, 512, 37, 91};
    const auto encoded = EncodeGameplayState(state);
    Check(DecodeGameplayState(encoded).Garden == state.Garden, "PixelGameplay2 garden growth did not round trip");
    auto legacy = encoded;
    const auto firstLineEnd = legacy.find('\n');
    Check(firstLineEnd != std::string::npos, "gameplay codec header missing");
    legacy.replace(0, firstLineEnd, "PixelGameplay 1");
    const auto gardenLine = legacy.rfind("\n1 0 512 37 91\n");
    Check(gardenLine != std::string::npos, "gameplay v2 garden record missing");
    legacy.erase(gardenLine + 1);
    const auto decodedLegacy = DecodeGameplayState(legacy);
    Check(decodedLegacy.Garden == ForceGardenState{}, "PixelGameplay1 did not default garden state");
    auto invalid = encoded;
    const auto growthLine = invalid.rfind("\n1 0 512 37 91\n");
    invalid.replace(growthLine + 1, std::string("1 0 512 37 91").size(), "1 0 512 181 0");
    bool rejected = false;
    try { (void)DecodeGameplayState(invalid); }
    catch (const WorldStorageError &) { rejected = true; }
    Check(rejected, "codec accepted force garden growth beyond its mature cap");

    SaveRoot root;
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    std::filesystem::path directory;
    {
        GameSession baseline(true, generation);
        Check(baseline.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded(), "small-window baseline save failed");
        directory = baseline.GetSaveDirectory();
    }
#if defined(_WIN32)
    _putenv_s("PIXEL_SIMULATION_WINDOW", "64");
#else
    setenv("PIXEL_SIMULATION_WINDOW", "64", 1);
#endif
    GameSession session(true, generation);
    const auto commit = session.CurrentCommit();
    const auto checkpoint = ReadText(directory / "checkpoint.current");
    const auto result = session.Control(NativeCaller::Driver, api::CreateForceGarden{});
    Check(result.Status == api::ControlStatus::Rejected && result.Rejection == api::Reason::LimitExceeded
        && session.CurrentCommit() == commit && session.GetSaveDirectory() == directory
        && ReadText(directory / "checkpoint.current") == checkpoint,
        "window-64 garden creation did not reject without changing the old world");
}

void VerifyPersistenceAndCrystalClear() {
    SaveRoot root;
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    generation.Seed = 0x47415244454e;
    uint64_t sourceHandle = 0, sourceOwner = 0, deviceId = 0, processId = 0, processVersion = 0;
    uint64_t sourceRemaining = 0, processActiveSteps = 0, processRemaining = 0;
    ForceGardenState savedGarden;
    DeviceSnapshot savedDevice;
    ContainerSnapshot savedBag;
    {
        GameSession session(true, generation);
        CreateGarden(session);
        Step(session);
        auto state = Garden(session);
        Check(state.State.WildGrowth > 0, "natural crystal did not start before checkpoint");
        const auto processes = session.GetFormulaRuntime().InspectProcesses();
        const auto crystalProcess = std::find_if(processes.begin(), processes.end(), [](const auto &process) {
            return process.Carrier == ProcessCarrierKind::NaturalSource && process.Subject == GardenCrystalSubject;
        });
        Check(crystalProcess != processes.end(), "natural crystal process was not active");
        sourceOwner = crystalProcess->State.Id;
        const auto sources = session.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto crystal = std::find_if(sources.begin(), sources.end(), [&](const auto &source) {
            return source.Owner == sourceOwner;
        });
        Check(crystal != sources.end(), "natural crystal field source was not retained");
        sourceHandle = crystal->Handle.Id;
        for (int x = 516; x <= 524; ++x) {
            const auto before = session.GetWorld().GetCell(x, state.State.SurfaceY + 2);
            Check(before.Type != CellTypeId::Air, "persistence scene seam missing");
            Check(session.Execute(NativeCaller::Driver,
                api::MineRegion{{x, state.State.SurfaceY + 2}, uint32_t(before.Type)}).Applied(),
                "persistence scenario could not mine an authored seam cell");
        }
        Check(session.GetWorld().GetCell(522, state.State.SurfaceY + 2).Type == CellTypeId::Air,
            "mined seam cell remained present before checkpoint");

        const auto placed = session.Execute(NativeCaller::Driver, api::PlaceDevice{{GardenDeviceX, state.State.SurfaceY - 2}});
        Check(placed.Applied(), "persistence fixture device placement failed");
        auto device = std::get<api::DeviceChanged>(placed.Result).Device;
        deviceId = device.Ref.Id;
        Check(session.Execute(NativeCaller::Driver, api::SupplyDevice{device.Ref, 2}).Applied(), "persistence fixture supply failed");
        device = session.GetDevices().Find(deviceId)->Value;
        Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{device.Ref, true}).Applied(), "persistence fixture enable failed");
        Check(bool(session.ApplyProcess(NativeCaller::Driver, ForceGardenProgram())), "home process apply failed");
        for (int i = 0; i < 12; ++i) Step(session);

        const auto playerPosition = session.GetPlayer()->GetWorldPosition();
        const auto beforeAway = Garden(session).State;
        const auto beforeAwaySource = std::find_if(session.GetWorld().GetEnvironment().GetFieldSources().Sources().begin(),
            session.GetWorld().GetEnvironment().GetFieldSources().Sources().end(), [&](const auto &source) {
                return source.Handle.Id == sourceHandle;
            });
        Check(beforeAwaySource != session.GetWorld().GetEnvironment().GetFieldSources().Sources().end(),
            "crystal source disappeared before leaving the garden");
        const auto frozenLifetime = beforeAwaySource->Remaining;
        auto &world = testing::SessionAccess::World(session);
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(8192, 508));
        world.LoadSimulationNow();
        testing::SessionAccess::PublishFixtureChanges(session);
        session.Tick(0, {});
        const auto awaySource = std::find_if(world.GetEnvironment().GetFieldSources().Sources().begin(),
            world.GetEnvironment().GetFieldSources().Sources().end(), [&](const auto &source) {
                return source.Handle.Id == sourceHandle;
            });
        Check(awaySource != world.GetEnvironment().GetFieldSources().Sources().end() && awaySource->Suspended,
            "crystal source did not freeze outside its active domain");
        for (int i = 0; i < 4; ++i) Step(session);
        Check(Garden(session).State.WildGrowth == beforeAway.WildGrowth
            && Garden(session).State.HomeGrowth == beforeAway.HomeGrowth,
            "garden accumulated growth while its scene was inactive");
        const auto frozenSource = std::find_if(world.GetEnvironment().GetFieldSources().Sources().begin(),
            world.GetEnvironment().GetFieldSources().Sources().end(), [&](const auto &source) {
                return source.Handle.Id == sourceHandle;
            });
        Check(frozenSource != world.GetEnvironment().GetFieldSources().Sources().end()
            && frozenSource->Remaining == frozenLifetime, "inactive source lifetime caught up");
        testing::SessionAccess::SetPlayerPosition(session, playerPosition);
        world.LoadSimulationNow();
        testing::SessionAccess::PublishFixtureChanges(session);
        session.Tick(0, {});
        const auto resumed = Garden(session).State;
        Check(resumed.WildGrowth == beforeAway.WildGrowth && resumed.HomeGrowth == beforeAway.HomeGrowth,
            "returning to the garden simulated missed growth steps");
        Check(std::any_of(world.GetEnvironment().GetFieldSources().Sources().begin(),
            world.GetEnvironment().GetFieldSources().Sources().end(), [&](const auto &source) {
                return source.Handle.Id == sourceHandle && !source.Suspended;
            }), "returning changed or failed to resume the crystal identity");
        Step(session);
        Check(Garden(session).State.WildGrowth == beforeAway.WildGrowth + 1,
            "garden did not resume by one committed step after return");
        savedGarden = Garden(session).State;
        savedDevice = session.GetDevices().Find(deviceId)->Value;
        savedBag = session.GetInventory().Inspect(session.PlayerInventory());
        const auto savedSources = session.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto savedCrystal = std::find_if(savedSources.begin(), savedSources.end(), [&](const auto &source) {
            return source.Owner == sourceOwner;
        });
        Check(savedCrystal != savedSources.end(), "crystal source missing at save boundary");
        sourceRemaining = savedCrystal->Remaining;
        const auto savedProcesses = session.GetFormulaRuntime().InspectProcesses();
        const auto savedNaturalProcess = std::find_if(savedProcesses.begin(), savedProcesses.end(), [](const auto &process) {
            return process.Carrier == ProcessCarrierKind::NaturalSource && process.Subject == GardenCrystalSubject;
        });
        Check(savedNaturalProcess != savedProcesses.end(), "crystal continuation missing at save boundary");
        processId = savedNaturalProcess->State.Id;
        processVersion = savedNaturalProcess->Version;
        processActiveSteps = savedNaturalProcess->State.ActiveSteps;
        processRemaining = savedNaturalProcess->State.Remaining;
        Check(session.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded(), "garden checkpoint save failed");
    }
    {
        GameSession loaded(true, generation);
        const auto state = Garden(loaded);
        const auto sources = loaded.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto device = loaded.GetDevices().Find(deviceId);
        Check(state.State == savedGarden, "checkpoint changed accumulated garden growth");
        Check(device && device->Value == savedDevice, "checkpoint changed device identity, cycles, or fuel");
        Check(loaded.GetInventory().Inspect(loaded.PlayerInventory()) == savedBag,
            "checkpoint duplicated or lost player inventory");
        const auto crystal = std::find_if(sources.begin(), sources.end(), [&](const auto &source) {
            return source.Owner == sourceOwner;
        });
        Check(crystal != sources.end() && crystal->Handle.Id == sourceHandle && crystal->Remaining == sourceRemaining
            && !crystal->Suspended, "checkpoint changed crystal identity or source lifetime");
        const auto loadedProcesses = loaded.GetFormulaRuntime().InspectProcesses();
        const auto naturalProcess = std::find_if(loadedProcesses.begin(), loadedProcesses.end(), [](const auto &process) {
            return process.Carrier == ProcessCarrierKind::NaturalSource && process.Subject == GardenCrystalSubject;
        });
        Check(naturalProcess != loadedProcesses.end() && naturalProcess->State.Id == processId
            && naturalProcess->Version == processVersion && naturalProcess->State.ActiveSteps == processActiveSteps
            && naturalProcess->State.Remaining == processRemaining,
            "checkpoint changed crystal continuation identity or progress");
        Check(loaded.Control(NativeCaller::Driver, api::ClearRunning{}).Succeeded(), "clear running failed");
        const auto cleared = loaded.GetWorld().GetEnvironment().GetFieldSources().Sources();
        Check(std::none_of(cleared.begin(), cleared.end(), [&](const auto &source) {
            return source.Handle.Id == sourceHandle || source.Owner == sourceOwner;
        }),
            "clearing running retained the natural crystal source");
        Check(!Garden(loaded).State.CrystalEnabled, "clearing running did not disable crystal restart");
        for (int i = 0; i < 20; ++i) Step(loaded);
        const auto afterClear = loaded.GetWorld().GetEnvironment().GetFieldSources().Sources();
        Check(std::none_of(afterClear.begin(), afterClear.end(), [&](const auto &source) {
            return source.Handle.Id == sourceHandle || source.Owner == sourceOwner;
        }),
            "cleared natural crystal respawned");
        Check(loaded.GetWorld().GetCell(522, Garden(loaded).State.SurfaceY + 2).Type == CellTypeId::Air,
            "loading or clearing re-seeded a mined ore cell");
        Check(loaded.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded(), "post-clear checkpoint failed");
    }
    GameSession restored(true, generation);
    Check(!Garden(restored).State.CrystalEnabled
        && restored.GetWorld().GetCell(522, Garden(restored).State.SurfaceY + 2).Type == CellTypeId::Air,
        "repeated load respawned the crystal or the authored vein");
}

void VerifyCrystalRenewalBoundary() {
    WorldGenerationSettings generation;
    generation.Kind = WorldGeneratorKind::Flat;
    generation.Seed = 0x600600;
    GameSession session(false, generation);
    CreateGarden(session);
    const auto findCrystal = [&] {
        const auto processes = session.GetFormulaRuntime().InspectProcesses();
        const auto processCount = std::count_if(processes.begin(), processes.end(), [](const auto &process) {
            return process.Carrier == ProcessCarrierKind::NaturalSource && process.Subject == GardenCrystalSubject;
        });
        Check(processCount == 1, "garden renewal created zero or multiple natural-source processes");
        const auto process = std::find_if(processes.begin(), processes.end(), [](const auto &candidate) {
            return candidate.Carrier == ProcessCarrierKind::NaturalSource && candidate.Subject == GardenCrystalSubject;
        });
        const auto sources = session.GetWorld().GetEnvironment().GetFieldSources().Sources();
        const auto source = std::find_if(sources.begin(), sources.end(), [&](const auto &candidate) {
            return candidate.Owner == process->State.Id;
        });
        Check(source != sources.end(), "natural-source process has no field source");
        Check(std::count_if(sources.begin(), sources.end(), [&](const auto &candidate) {
            return candidate.Owner == process->State.Id;
        }) == 1, "natural-source process owns duplicate field sources");
        return std::pair{process->State.Id, source->Handle.Id};
    };
    const auto original = findCrystal();
    for (int i = 0; i < 605; ++i) {
        Step(session);
        if (i == int(GardenMatureSteps) - 1)
            Check(Garden(session).State.WildGrowth == GardenMatureSteps,
                "wild growth did not reach its 180-step cap");
        const auto processes = session.GetFormulaRuntime().InspectProcesses();
        Check(std::count_if(processes.begin(), processes.end(), [](const auto &process) {
            return process.Carrier == ProcessCarrierKind::NaturalSource && process.Subject == GardenCrystalSubject;
        }) <= 1, "crystal restart briefly duplicated its natural-source process");
    }
    const auto renewed = findCrystal();
    const auto sources = session.GetWorld().GetEnvironment().GetFieldSources().Sources();
    Check(renewed.first != original.first && renewed.second != original.second
        && std::none_of(sources.begin(), sources.end(), [&](const auto &source) {
            return source.Handle.Id == original.second;
        }), "600-step natural crystal restart did not replace its process and source identity");
    Check(Garden(session).State.WildGrowth == GardenMatureSteps,
        "wild growth did not retain the mature cap across crystal renewal");
}
} // namespace

int main() {
    try {
        VerifyBedsAndForceThresholds();
        VerifyDeviceSupplyAndFuel();
        VerifyPauseAndPendingCommit();
        VerifyPersistenceAndCrystalClear();
        VerifyCrystalRenewalBoundary();
        VerifyGardenCodecAndSmallWindowRejection();
        std::cout << "Force garden CPU behavior contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
