#include "Gameplay/GameSession.h"
#include "Gameplay/Construction/DeviceCosts.h"
#include "LLMView/JsonAdapter.h"
#include "../Support/TestAccess.h"
#include <iostream>
#include <chrono>
#include <cstdlib>
#include <filesystem>

using namespace pixel;
namespace {
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
class TemporarySaveRoot {
  public:
    std::filesystem::path Path = std::filesystem::temp_directory_path()
        / ("pixel-authority-pending-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    TemporarySaveRoot() {
        std::filesystem::remove_all(Path);
        if (const auto *value = std::getenv("PIXEL_SAVE_ROOT")) PreviousRoot = value;
        if (const auto *value = std::getenv("PIXEL_SIMULATION_WINDOW")) PreviousWindow = value;
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", Path.string().c_str());
        _putenv_s("PIXEL_SIMULATION_WINDOW", "256");
#else
        setenv("PIXEL_SAVE_ROOT", Path.string().c_str(), 1);
        setenv("PIXEL_SIMULATION_WINDOW", "256", 1);
#endif
    }

    ~TemporarySaveRoot() {
#if defined(_WIN32)
        _putenv_s("PIXEL_SAVE_ROOT", PreviousRoot ? PreviousRoot->c_str() : "");
        _putenv_s("PIXEL_SIMULATION_WINDOW", PreviousWindow ? PreviousWindow->c_str() : "");
#else
        if (PreviousRoot) setenv("PIXEL_SAVE_ROOT", PreviousRoot->c_str(), 1);
        else unsetenv("PIXEL_SAVE_ROOT");
        if (PreviousWindow) setenv("PIXEL_SIMULATION_WINDOW", PreviousWindow->c_str(), 1);
        else unsetenv("PIXEL_SIMULATION_WINDOW");
#endif
        std::filesystem::remove_all(Path);
    }

  private:
    std::optional<std::string> PreviousRoot, PreviousWindow;
};

const char *Field = "process 1; emit field(cells(4), acceleration(100), acceleration(0), ticks(1200)); "
    "await result within ticks(1); wait ticks(1000);";
void Apply(GameSession &session, std::string_view source = Field) {
    auto program = formula::process::Compile("authority-test", source);
    if (const auto *error = std::get_if<formula::process::Diagnostic>(&program)) throw std::runtime_error(error->Message);
    Check(bool(session.ApplyProcess(NativeCaller::Driver, std::get<formula::process::ProgramArtifact>(program))), "apply process");
}
api::ActionRequest Request(GameSession &session, uint64_t sequence = 1, api::Rect scope = {{92, 92}, 32, 32}) {
    return {session.WorldEpoch(), sequence, api::InstallIntervention{session.AppliedProgram(), {100, 100}, scope}};
}
void Step(GameSession &session, int count = 1) {
    for (int i = 0; i < count; ++i) {
        Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "request successful step");
        session.BeginFrame();
        session.Tick(1.f / 60.f, {});
    }
}
DeviceRef Device(GameSession &session, api::Cell at = {100, 100}) {
    auto &inventory = testing::SessionAccess::Inventory(session);
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), ForceEmitterCost()) == items::Result::Applied, "fixture stock");
    const auto placed = session.Execute(NativeCaller::Input, api::PlaceDevice{at});
    Check(placed.Applied(), "device placement");
    return std::get<api::DeviceChanged>(placed.Result).Device.Ref;
}
DeviceRef Enable(GameSession &session, uint64_t id, bool enabled) {
    const auto ref = session.GetDevices().Find(id)->Value.Ref;
    const auto result = session.Execute(NativeCaller::Input, api::SetDeviceEnabled{ref, enabled});
    Check(result.Applied(), "device enable");
    return std::get<api::DeviceChanged>(result.Result).Device.Ref;
}
void RegionBoundaries() {
    authority::Control control;
    control.Reset(7);
    control.SetProtection({{1, 1}, 1, 100, 100, true}, true);
    Check(control.CanIntervene(7, {{83, 84}, 1, 1}) && control.CanIntervene(7, {{118, 84}, 1, 1}), "half-open outside boundary");
    Check(!control.CanIntervene(7, {{83, 84}, 2, 1}) && !control.CanObserve(7, {{117, 117}, 1, 1}), "overlap crossed protection");
    Check(!control.CanIntervene(8, {{0, 0}, 1, 1}) && !control.CanIntervene(7, {{INT64_MAX, 0}, 2, 1}), "identity or range bypass");
    Check(control.CanIntervene(7, {{-(int64_t{1} << 53), 0}, 64, 64}), "far signed coordinates lost precision");
    control.SetCapabilities({false, true});
    Check(!control.CanObserve(7, {{0, 0}, 1, 1}) && control.CanIntervene(7, {{0, 0}, 1, 1}), "observation and action conflated");
}
void ProtectionAndDelivery() {
    GameSession session;
    Apply(session);
    const auto subject = session.WorldWillSession();
    Check(session.Submit(subject, {session.WorldEpoch(), 1, api::MineRegion{{10, 10}, {}}}).Rejection == api::Reason::PermissionDenied,
        "world will acquired player capability");
    Check(session.Control(subject, api::ResetWorld{}).Rejection == api::Reason::PermissionDenied, "world will reset world");
    Check(session.Execute(NativeCaller::Driver, std::get<api::InstallIntervention>(Request(session).Payload)).Rejection == api::Reason::PermissionDenied,
        "native caller self-selected intervention identity");
    const auto proposal = Request(session, 2);
    const auto started = session.Submit(subject, proposal);
    Check(started.Applied() && session.GetWorld().GetForce(100, 100).X == 100, "intervention did not create actual force");
    const auto count = session.GetWorld().GetEnvironment().GetFieldSources().Sources().size();
    Check(session.Submit(subject, proposal).Applied() && session.GetWorld().GetEnvironment().GetFieldSources().Sources().size() == count,
        "duplicate proposal installed another source");
    const auto device = Device(session);
    Check(session.Execute(NativeCaller::Input, api::SetDeviceProtection{device, true}).Applied(), "protection module install");
    Check(session.GetWorld().GetForce(100, 100).X == 100, "disabled protection affected control");

    llm::ViewService views(session);
    llm::Policy policy;
    policy.AuthoritySubject = subject;
    policy.Region = api::Rect{{80, 80}, 64, 64};
    policy.Topics[size_t(api::Topic::Materials)] = true;
    const auto observer = views.RegisterObserver(policy);
    const api::Rect scope{{96, 96}, 8, 8};
    const auto open = views.OpenView(observer, scope);
    Check(open.View.has_value(), "authorized regional view");
    const auto pending = views.Query(observer, open.View->Id, {api::Topic::Materials, scope});
    Check(pending.Result == llm::Status::Ok, "prepare regional result");
    const auto enabled = Enable(session, device.Id, true);
    Check(views.Deliver(observer, pending.Delivery).Result == llm::Status::ViewRevoked, "prepared query survived protection");
    Check(views.OpenView(observer, scope).Result == llm::Status::PermissionDenied, "protected view reopened");
    Check(session.GetWorld().GetForce(100, 100).X == 0 && session.GetFormulaRuntime().ProcessStats().Instances == 0
        && session.GetFormulaRuntime().ProcessStats().PendingEvents == 0, "protection leaked owned source or future work");
    Check(session.Submit(subject, Request(session, 3)).Rejection == api::Reason::PermissionDenied, "protected admission succeeded");
    const auto history = session.CaptureKnowledgeData();
    bool stop = false;
    for (const auto &record : history.History)
        if (const auto *value = std::get_if<api::InterventionStopped>(&record.Source.Payload)) {
            stop = value->Reason == api::InterventionStopReason::ControlRevoked && value->Subject == subject;
            llm::Response response;
            response.Facts.emplace();
            response.Facts->Facts.push_back(record.Source);
            Check(llm::ToJson(response).dump().find("control_revoked") != std::string::npos, "stop reason absent from JSON");
        }
    Check(stop, "revocation absent from important history");
    Check(session.Execute(NativeCaller::Input, api::SetDeviceProtection{device, false}).Rejection == api::Reason::PreconditionFailed,
        "old device revision controlled module");
    Check(session.Execute(NativeCaller::Input, api::RemoveDevice{enabled}).Applied(), "protected device removal");
    Check(views.OpenView(observer, scope).Result == llm::Status::Ok, "removed device still protects");
    const auto replacement = Device(session);
    Check(replacement.Id != device.Id && session.Execute(NativeCaller::Input, api::SetDeviceProtection{enabled, true}).Rejection
        == api::Reason::PreconditionFailed, "deleted device identity reused");
    Enable(session, replacement.Id, true);
    Check(session.GetAuthority().CanIntervene(subject, scope), "replacement inherited old module");
    Check(session.Submit(subject, Request(session, 4)).Applied(), "control did not recover");
    Check(session.GetFormulaRuntime().ProcessStats().Instances == 1, "revoked root resumed after control recovery");
    const auto oldEpoch = session.WorldEpoch();
    Check(session.Control(NativeCaller::Driver, api::ResetWorld{}).Succeeded(), "reset");
    Check(session.WorldEpoch() != oldEpoch && session.Submit(subject, proposal).Rejection == api::Reason::CallerExpired,
        "replaced world reused authority identity");
}
void CapabilitiesAndDescendants() {
    GameSession session;
    Apply(session, "process 1; emit field(cells(4), acceleration(100), acceleration(0), ticks(100)); "
        "emit bolt(1, speed(60), ticks(60)); await hit within ticks(70); emit field(cells(4), acceleration(200), acceleration(0), ticks(100)); wait ticks(50);");
    const auto subject = session.WorldWillSession();
    Check(session.Submit(subject, Request(session)).Applied(), "descendant fixture start");
    Check(session.GetFormulaRuntime().ProcessStats().Projectiles == 1, "descendant bolt missing");
    Check(session.SetWorldWillCapabilities(NativeCaller::Input, {false, false}).Rejection == api::Reason::PermissionDenied, "player granted role");
    session.SetWorldWillCapabilities(NativeCaller::Driver, {false, true});
    Check(session.GetWorld().GetForce(100, 100).X == 100, "observation revoke stopped permitted action");
    session.SetWorldWillCapabilities(NativeCaller::Driver, {true, false});
    const auto stats = session.GetFormulaRuntime().ProcessStats();
    Check(stats.Instances == 0 && stats.Projectiles == 0 && stats.PendingEvents == 0
        && session.GetWorld().GetEnvironment().GetFieldSources().Sources().empty(), "capability revoke leaked descendants");
    Step(session, 3);
    Check(session.GetWorld().GetForce(100, 100).X == 0, "delayed effect escaped revoked parent");
    session.SetWorldWillCapabilities(NativeCaller::Driver, {true, true});
    Apply(session, "process 1; emit bolt(1, speed(600), ticks(20)); await hit within ticks(25); emit field(cells(16), acceleration(200), acceleration(0), ticks(100)); wait ticks(50);");
    // A projectile exiting its inherited scope cannot move the continuation
    // outside it; the oversized descendant field is rejected by the host.
    Check(session.Submit(subject, Request(session, 2, {{96, 96}, 8, 8})).Applied(), "bounded descendant start");
    Step(session, 4);
    Check(session.GetWorld().GetEnvironment().GetFieldSources().Sources().empty(), "descendant expanded inherited scope");
    Check(session.GetFormulaRuntime().ProcessReceipts().back().Refusal == ProcessRefusal::Scope, "scope refusal missing");
}
void OwnershipIsolation() {
    GameSession session;
    Apply(session);
    ProcessCarrier natural;
    natural.Kind = ProcessCarrierKind::NaturalSource;
    natural.Subject = 77;
    natural.Origin = WorldPosition::FromAbsolute(100, 100);
    natural.Scope = {92, 92, 32, 32};
    Check(session.StartProcessFixture(NativeCaller::Driver, natural).Succeeded(), "natural source start");
    Check(session.Submit(session.WorldWillSession(), Request(session)).Applied()
        && session.GetWorld().GetForce(100, 100).X == 200, "independent source composition");
    session.SetWorldWillCapabilities(NativeCaller::Driver, {true, false});
    Check(session.GetWorld().GetForce(100, 100).X == 100
        && session.GetFormulaRuntime().HasCarrier(ProcessCarrierKind::NaturalSource, 77)
        && session.GetWorld().GetEnvironment().GetFieldSources().Sources().size() == 1,
        "revocation removed another owner's field");
}
void FrozenAndExpired() {
    GameSession session;
    Apply(session);
    Check(session.Submit(session.WorldWillSession(), Request(session)).Applied(), "frozen start");
    auto &world = testing::SessionAccess::World(session);
    // Lose residency while retaining the root and its source definition.
    testing::WorldAccess::Map(world).Remove(ChunkOf({100, 100}));
    testing::SessionAccess::PublishFixtureChanges(session);
    Check(session.GetFormulaRuntime().ProcessStats().SuspendedInstances == 1, "root did not freeze");
    session.SetWorldWillCapabilities(NativeCaller::Driver, {true, false});
    Check(session.GetFormulaRuntime().ProcessStats().Instances == 0 && world.GetEnvironment().GetFieldSources().Sources().empty(),
        "frozen root survived revoke");

    GameSession expiry;
    Apply(expiry);
    Check(expiry.Submit(expiry.WorldWillSession(), Request(expiry)).Applied(), "expiry start");
    // Advance the fixture clock to the last permitted step, avoiding 599
    // unrelated terrain steps. The final step exercises production expiry.
    testing::SessionAccess::RestoreClock(expiry, 599 * SimulationCoordinator::StepSeconds, 0);
    Step(expiry);
    Check(expiry.GetFormulaRuntime().ProcessStats().Instances == 0 && expiry.GetWorld().GetForce(100, 100).X == 0, "lease did not clear source");
    const auto history = expiry.CaptureKnowledgeData();
    Check(std::get<api::InterventionStopped>(history.History.back().Source.Payload).Reason == api::InterventionStopReason::LeaseExpired,
        "lease expiry reason missing");
}
void PendingAndPersistence() {
    TemporarySaveRoot saveRoot;
    GameSession session(true);
    Apply(session);
    const auto subject = session.WorldWillSession();
    const api::Rect scope{{580, 480}, 48, 32};
    const auto proposal = api::ActionRequest{session.WorldEpoch(), 1,
        api::InstallIntervention{session.AppliedProgram(), {600, 500}, scope}};
    Check(session.Submit(subject, proposal).Applied(), "persistent intervention start");
    const auto epoch = session.WorldEpoch();
    Check(session.Control(NativeCaller::Driver, api::SaveWorld{}).Rejection == api::Reason::Unsupported
        && session.Control(NativeCaller::Driver, api::ResetWorld{}).Rejection == api::Reason::Unsupported
        && session.WorldEpoch() == epoch, "transient authority silently entered checkpoint");

    auto &world = testing::SessionAccess::World(session);
    WindStrategySettings wind;
    wind.Kind = WindStrategyKind::Procedural;
    world.ConfigureWindDomain(256, wind);
    for (const auto x : {600, 602, 604, 606}) world.SetCell(x, 504, {CellTypeId::Dirt, {0, 1}});
    world.SetBaseForce(600, 504, {0, 3600});
    world.SetBaseForce(602, 504, {3600, 0});
    world.SetBaseForce(604, 504, {-3600, 0});
    world.SetBaseForce(606, 504, {0, -3600});
    testing::WorldAccess::Map(world).Wake(world.GetSimulationBounds());
    testing::SessionAccess::PublishFixtureChanges(session);
    world.WaitForStreaming();
    session.Tick(0, {});
    const auto *stream = world.GetStreamer();
    const auto bounds = world.GetSimulationBounds();
    for (const auto coordinate : testing::WorldAccess::Map(world).ChunkCoordinates()) {
        const WorldRect chunk{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge,
            storage::ChunkEdge, storage::ChunkEdge};
        if (!chunk.Intersects(bounds)) testing::WorldAccess::Map(world).Remove(coordinate);
    }
    auto publication = stream->Settings().BudgetBytes - stream->RetainedBytes() - 128;
    world.SetPublicationBytes(publication);
    world.WaitForStreaming();
    session.Tick(0, {});
    // ServiceStreaming may accept optional prefetches after the first purge.
    // Drain them, then remove every unpinned chunk before saturating the budget.
    world.WaitForStreaming();
    for (const auto coordinate : testing::WorldAccess::Map(world).ChunkCoordinates()) {
        const WorldRect chunk{coordinate.X * storage::ChunkEdge, coordinate.Y * storage::ChunkEdge,
            storage::ChunkEdge, storage::ChunkEdge};
        if (!chunk.Intersects(bounds)) testing::WorldAccess::Map(world).Remove(coordinate);
    }
    publication += stream->Settings().BudgetBytes - stream->RetainedBytes() - 128;
    world.SetPublicationBytes(publication);
    testing::SessionAccess::PublishFixtureChanges(session);
    const auto stamp = session.CurrentCommit();
    const auto authorization = session.GetAuthority().Version();
    Step(session);
    Check(world.GetEnvironment().IsWaitingForBudget() && session.CurrentCommit() == stamp, "real pending step not reached");
    Check(session.SetWorldWillCapabilities(NativeCaller::Driver, {false, false}).Rejection == api::Reason::WaitingForBudget,
        "authority mutated prepared environment");
    auto second = proposal;
    second.Sequence = 2;
    Check(session.Submit(subject, second).Rejection == api::Reason::WaitingForBudget
        && session.GetAuthority().Version() == authorization
        && session.GetFormulaRuntime().ProcessStats().Instances == 1, "pending admission changed authority or programs");
    world.SetPublicationBytes(0);
    session.Tick(.1f, {});
    Check(!world.GetEnvironment().IsWaitingForBudget() && session.CurrentCommit().Tick == stamp.Tick + 1,
        "pending retry was not exactly one successful step");
    session.SetWorldWillCapabilities(NativeCaller::Driver, {true, false});
    Check(world.GetEnvironment().GetFieldSources().Sources().empty(), "post-pending revoke left source");
    session.SetWorldWillCapabilities(NativeCaller::Driver, {true, true});
    Check(session.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded(), "cleaned authority prevented ordinary save");
    // Historical process receipts may mention interventions; restoring them must
    // not restore their terminated runtime or mint new authority.
    GameSession restored(true);
    Check(restored.GetFormulaRuntime().ProcessStats().Instances == 0
        && restored.GetWorld().GetEnvironment().GetFieldSources().Sources().empty(), "checkpoint revived transient intervention");
    const auto device = Device(restored, {580, 490});
    Check(restored.Execute(NativeCaller::Input, api::SetDeviceProtection{device, true}).Applied(), "persistent module install");
    Check(restored.Control(NativeCaller::Driver, api::SaveWorld{}).Rejection == api::Reason::Unsupported, "checkpoint silently dropped module");
    Check(restored.Execute(NativeCaller::Input, api::SetDeviceProtection{device, false}).Applied(), "module removal");
    Check(restored.Control(NativeCaller::Driver, api::SaveWorld{}).Succeeded(), "removed module prevented checkpoint");
}
}
int main() {
    try {
        RegionBoundaries(); ProtectionAndDelivery(); CapabilitiesAndDescendants(); FrozenAndExpired(); OwnershipIsolation(); PendingAndPersistence();
        std::cout << "authority regions, admission, delivery, revocation, descendants and leases passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
