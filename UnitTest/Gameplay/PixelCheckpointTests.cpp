#include "../Support/TestAccess.h"
#include "../Support/CheckpointFiles.h"
#include "Runtime/Gameplay/Persistence/GameplaySaveData.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace {
using namespace pixel;
using namespace pixel::items;
namespace process = formula::process;
void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
struct EnvironmentVariable {
    std::string Name;
    std::optional<std::string> Previous;
    static void Set(const std::string &name, const char *value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value ? value : "");
#else
        if (value) setenv(name.c_str(), value, 1); else unsetenv(name.c_str());
#endif
    }
    EnvironmentVariable(std::string name, const std::string &value) : Name(std::move(name)) {
        if (const auto *old = std::getenv(Name.c_str())) Previous = old;
        Set(Name, value.c_str());
    }
    ~EnvironmentVariable() { Set(Name, Previous ? Previous->c_str() : nullptr); }
};
struct SaveRoot {
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-checkpoint-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    EnvironmentVariable Root{"PIXEL_SAVE_ROOT", Path.string()}, Window{"PIXEL_SIMULATION_WINDOW", "256"};
    ~SaveRoot() { std::filesystem::remove_all(Path); }
};
void Step(GameSession &session) {
    session.BeginFrame();
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "step rejected");
    session.Tick(1.f / 60, {});
}
void Save(GameSession &session) {
    const auto result = session.Control(NativeCaller::Driver, api::SaveWorld{});
    if (!result.Succeeded()) throw std::runtime_error("save failed: " + result.Message);
}
void Stock(GameSession &session, ItemId item, uint32_t quantity) {
    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array amounts{ItemAmount{item, quantity}};
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), amounts) == Result::Applied, "stock failed");
}
DeviceSnapshot Device(GameSession &session, uint64_t id) { return session.GetDevices().Find(id)->Value; }
uint64_t BuildEmitter(GameSession &session) {
    Stock(session, ItemId::Stone, 4); Stock(session, ItemId::CopperOre, 2); Stock(session, ItemId::Coal, 3);
    const auto placed = session.Execute(NativeCaller::Driver, api::PlaceDevice{{500, 470}});
    Check(placed.Applied(), "device placement failed");
    auto device = std::get<api::DeviceChanged>(placed.Result).Device;
    Check(session.Execute(NativeCaller::Driver, api::SupplyDevice{device.Ref, 3}).Applied(), "supply failed");
    device = Device(session, device.Ref.Id);
    Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{device.Ref, true}).Applied(), "enable failed");
    Step(session);
    return device.Ref.Id;
}
process::ProgramArtifact Program(std::string_view text) {
    auto compiled = process::Compile("checkpoint-test", text);
    if (const auto *error = std::get_if<process::Diagnostic>(&compiled)) throw std::runtime_error(error->Message);
    return std::get<process::ProgramArtifact>(compiled);
}

void ActiveDomainsResumeOnce() {
    SaveRoot files;
    DeviceSnapshot savedDevice;
    items::ContainerSnapshot savedBag;
    ActorsState savedActors;
    std::vector<FieldSourceSnapshot> savedSources;
    std::vector<ProcessInspection> savedProcesses;
    uint64_t id, tick;
    size_t pending;
    std::filesystem::path directory;
    {
        GameSession session(true);
        auto &world = testing::SessionAccess::World(session);
        id = BuildEmitter(session);
        const auto actor = testing::SessionAccess::Actors(session).Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(550, 450));
        Check(actor.has_value(), "resident spawn failed");
        auto &actors = testing::SessionAccess::Actors(session);
        const auto discarded = actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(560, 450));
        Check(discarded && actors.Remove(*discarded), "retired actor setup failed");
        auto &inventory = testing::SessionAccess::Inventory(session);
        const auto retiredContainer = inventory.Create(64);
        auto &devices = testing::SessionAccess::Devices(session);
        const auto retiredDevice = devices.Create(540, 470, actors.PlayerSnapshot()->Id.Value, retiredContainer);
        devices.Remove(retiredDevice);
        inventory.Destroy(retiredContainer);
        Step(session);
        auto &bridge = testing::SessionAccess::Runtime(session);
        const auto program = Program("process 1; emit dig(cells(0)); wait ticks(1); emit bolt(1, speed(60), ticks(12)); "
                                     "await hit within ticks(15); emit dig(cells(0));");
        bridge.ApplyProcess(program);
        ProcessCarrier carrier;
        carrier.Kind = ProcessCarrierKind::Player;
        carrier.Subject = session.GetActors().PlayerSnapshot()->Id.Value;
        carrier.Origin = WorldPosition::FromAbsolute(450.5, 450.5);
        carrier.Scope = world.GetSimulationBounds();
        world.SetCell(450, 450, {CellTypeId::Stone});
        Check(bridge.CastCarrier(world, carrier), "checkpoint process failed");
        Step(session); // Launch from already committed empty terrain, preserving batch collision rules.
        world.SetCell(450, 450, {CellTypeId::CopperOre}); // Would disappear if activation replayed.
        Save(session); // Bolt receipt is queued, not yet delivered.
        savedDevice = Device(session, id);
        savedBag = session.GetInventory().Inspect(session.PlayerInventory());
        savedActors = session.GetActors().CaptureState();
        const auto sources = world.GetEnvironment().GetFieldSources().Sources();
        savedSources.assign(sources.begin(), sources.end());
        savedProcesses = bridge.InspectProcesses();
        pending = bridge.ProcessStats().PendingEvents;
        tick = session.CurrentCommit().Tick;
        directory = session.GetSaveDirectory();
        const auto &metrics = session.LastCheckpointMetrics();
        std::cout << "checkpoint capture_us=" << metrics.CaptureMicroseconds << " write_us=" << metrics.WriteMicroseconds
                  << " total_pause_us=" << metrics.TotalMicroseconds << " payload_bytes=" << metrics.PayloadBytes << '\n';
    }
    GameSession loaded(true);
    Check(loaded.CurrentCommit().Tick == tick && Device(loaded, id).Instance == savedDevice.Instance
        && Device(loaded, id).Cycles == savedDevice.Cycles && Device(loaded, id).Fuel == savedDevice.Fuel
        && loaded.GetInventory().Inspect(loaded.PlayerInventory()) == savedBag, "checkpoint replayed device resources");
    Check(loaded.GetFormulaRuntime().ProcessStats().PendingEvents == pending
        && loaded.GetFormulaRuntime().Projectiles().size() == 1
        && loaded.GetWorld().GetCell(450, 450).Type == CellTypeId::CopperOre, "queued work lost or activation replayed");
    for (const auto &actor : savedActors.Records) {
        const auto *restored = loaded.GetActors().Find(actor.Id);
        const auto original = actor.Body.CaptureState();
        Check(restored && restored->Kind == actor.Kind && restored->Body.CaptureState().VerticalSpeed == original.VerticalSpeed
            && restored->Body.GetWorldPosition().Local == original.Position.Local, "actor momentum or identity lost");
    }
    const auto sources = loaded.GetWorld().GetEnvironment().GetFieldSources().Sources();
    Check(sources.size() == savedSources.size() && sources[0].Handle == savedSources[0].Handle
        && sources[0].Remaining == savedSources[0].Remaining, "field identity/lifetime changed");
    const auto restoredProcesses = loaded.GetFormulaRuntime().InspectProcesses();
    Check(restoredProcesses.size() == savedProcesses.size(), "missing program instance");
    for (size_t i = 0; i < savedProcesses.size(); ++i)
        Check(restoredProcesses[i].State.Id == savedProcesses[i].State.Id
            && restoredProcesses[i].State.Instruction == savedProcesses[i].State.Instruction
            && restoredProcesses[i].Credits == savedProcesses[i].Credits, "program continuation changed");
    auto &actors = testing::SessionAccess::Actors(loaded);
    const auto newActor = actors.Spawn(ActorKind::Resident, WorldPosition::FromAbsolute(560, 450));
    Check(newActor && actors.Require(*newActor).Id.Value == savedActors.NextId, "retired actor ID was reused");
    auto &inventory = testing::SessionAccess::Inventory(loaded);
    const auto newContainer = inventory.Create(64);
    Check(newContainer.Value == 4, "retired container ID was reused");
    const auto newDevice = testing::SessionAccess::Devices(loaded).Create(540, 470, actors.PlayerSnapshot()->Id.Value, newContainer);
    Check(newDevice.Id == 3, "retired device ID was reused");
    Step(loaded);
    Check(Device(loaded, id).Cycles == 1 && Device(loaded, id).Fuel == 2
        && loaded.GetWorld().GetCell(450, 450).Type == CellTypeId::CopperOre,
        "first resumed step replayed payment or mining");
    for (int i = 0; i < 27; ++i) Step(loaded);
    Check(Device(loaded, id).Cycles == 2 && Device(loaded, id).Fuel == 1,
        "saved device did not finish exactly its prepaid cycle");
    Save(loaded);
    Check(testing::CheckpointDirectory(directory) != directory, "save did not publish a generation");
}

void FrozenAndUnloadedChangesAreAtomic() {
    SaveRoot files;
    SessionStore store(files.Path);
    std::filesystem::path directory, previous;
    uint64_t deviceId, sourceId, remaining;
    {
        GameSession session(true);
        auto &world = testing::SessionAccess::World(session);
        deviceId = BuildEmitter(session);
        world.SetCell(450, 450, {CellTypeId::GoldOre});
        Save(session);
        directory = session.GetSaveDirectory();
        previous = store.ResolveCheckpoint(directory);
        world.SetCell(450, 450, {CellTypeId::TungstenOre});
        world.Save(); // Flushed working chunk is later unloaded, never overwrites previous.
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(8192, 508));
        world.LoadSimulationNow();
        testing::SessionAccess::PublishFixtureChanges(session);
        testing::WorldAccess::Map(world).Remove(ChunkOf({450, 450}));
        Check(!world.TryGetCell(450, 450), "dirty fixture was not unloaded");
        const auto source = world.GetEnvironment().GetFieldSources().Sources()[0];
        sourceId = source.Handle.Id;
        remaining = source.Remaining;
        Check(source.Suspended && Device(session, deviceId).State == DeviceState::OutsideActiveArea, "device not frozen");
        for (int i = 0; i < 3; ++i) Step(session);
        std::filesystem::create_directory(directory / "checkpoint.current.tmp");
        Check(!session.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded()
            && store.ResolveCheckpoint(directory) == previous && session.IsReady(), "interrupted save published partial generation");
        {
            GameSession old(true);
            Check(old.GetWorld().GetCell(450, 450).Type == CellTypeId::GoldOre
                && Device(old, deviceId).Cycles == 1 && Device(old, deviceId).Fuel == 2,
                "failed save modified old generation domain or block data");
        }
        std::filesystem::remove(directory / "checkpoint.current.tmp");
        Save(session);
    }
    GameSession loaded(true);
    auto &world = testing::SessionAccess::World(loaded);
    const auto frozen = world.GetEnvironment().GetFieldSources().Sources()[0];
    Check(frozen.Handle.Id == sourceId && frozen.Remaining == remaining && frozen.Suspended
        && !world.TryGetCell(450, 450), "frozen checkpoint aged or loaded remote program");
    testing::SessionAccess::SetPlayerPosition(loaded, WorldPosition::FromAbsolute(511, 508));
    world.LoadSimulationNow();
    testing::SessionAccess::PublishFixtureChanges(loaded);
    Check(world.GetCell(450, 450).Type == CellTypeId::TungstenOre
        && world.GetEnvironment().GetFieldSources().Sources()[0].Handle.Id == sourceId
        && !world.GetEnvironment().GetFieldSources().Sources()[0].Suspended
        && Device(loaded, deviceId).Cycles == 1 && Device(loaded, deviceId).Fuel == 2,
        "unloaded working block or frozen continuation failed to restore");
    Save(loaded);
    Check(store.ResolveCheckpoint(directory) != previous, "retry failed to publish new generation");
}

void InvalidDomainLinksFailExplicitly() {
    SaveRoot files;
    SessionStore store(files.Path);
    std::filesystem::path directory, checkpoint;
    {
        GameSession session(true);
        BuildEmitter(session);
        Save(session);
        directory = session.GetSaveDirectory();
        checkpoint = store.ResolveCheckpoint(directory);
    }
    const auto original = DecodeGameplayState(store.ReadGameplayState(checkpoint));
    auto invalid = original;
    invalid.Devices.Records[0].Value.Instance += 100;
    store.PublishCheckpoint(directory, checkpoint, store.ReadMetadata(checkpoint), EncodeGameplayState(invalid));
    bool rejected = false;
    try { GameSession loaded(true); }
    catch (const WorldStorageError &) { rejected = true; }
    Check(rejected, "missing program carrier silently restarted");
    store.PublishCheckpoint(directory, checkpoint, store.ReadMetadata(checkpoint), EncodeGameplayState(original));
    GameSession repaired(true);
    Check(repaired.GetDevices().Count() == 1, "valid checkpoint could not load after explicit repair");
}

void MigratedWorldNeverFallsBackSilently() {
    SaveRoot files;
    std::filesystem::path directory;
    {
        GameSession initial(true);
        directory = initial.GetSaveDirectory();
    }
    testing::ExportLegacyFixture(directory);
    {
        GameSession imported(true);
        Stock(imported, ItemId::GoldOre, 7);
        Save(imported);
    }
    std::filesystem::remove(directory / "checkpoint.current");
    bool rejected = false;
    try { GameSession lostPointer(true); }
    catch (const WorldStorageError &) { rejected = true; }
    Check(rejected, "missing migrated pointer silently restored legacy empty inventory");
    Check(std::filesystem::exists(directory / "world.meta"), "migration destroyed original legacy metadata");
}
}
int main() {
    try {
        ActiveDomainsResumeOnce();
        FrozenAndUnloadedChangesAreAtomic();
        InvalidDomainLinksFailExplicitly();
        MigratedWorldNeverFallsBackSilently();
#if defined(__unix__) || defined(__APPLE__)
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
        std::cout << "process_peak_rss_bytes=" << usage.ru_maxrss << '\n';
#else
        std::cout << "process_peak_rss_bytes=" << usage.ru_maxrss * 1024 << '\n';
#endif
#endif
        std::cout << "Atomic multi-domain checkpoint and continuation tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
