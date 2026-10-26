#include "../Support/TestAccess.h"
#include "Runtime/Render/DeviceFrame.h"
#include "Runtime/Render/GamePresentation.h"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <set>
#include <stdexcept>

namespace {
using namespace pixel;
void Check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void DeviceProjection() {
    Camera2D camera;
    camera.SetWorldPosition({{int64_t{1} << 46, -(int64_t{1} << 46)}, {8.125, 9.25}});
    const auto reference = camera.GetWorldPosition().Cell();
    std::vector<DeviceSnapshot> devices(3);
    devices[0].Ref = {7, 1};
    devices[0].X = reference.X + 2;
    devices[0].Y = reference.Y + 1;
    devices[0].State = DeviceState::Running;
    devices[1] = devices[0];
    devices[1].X += 1000;
    devices[2] = devices[0];
    devices[2].X = INT64_MIN;
    WorldGridFrame first, second;
    BuildDeviceFrame(devices, camera, {160, 120}, first);
    Check(first.DeviceDraws.size() == 1, "offscreen device was published");
    const auto draw = first.DeviceDraws.front();
    Check(draw.DeviceId == 7 && draw.CenterRadius == std::array<float, 4>{126, 88, 16, 16},
          "device projection lost precision in a distant world");
    Check(draw.OutputCenterRadius == std::array<float, 4>{138, 88, 4, 6.4f},
          "device output marker is not on the right edge");
    devices[0].X += 1;
    BuildDeviceFrame(devices, camera, {160, 120}, second);
    Check(first.DeviceDraws.front().CenterRadius[0] == 126 && second.DeviceDraws.front().CenterRadius[0] == 142,
          "published devices share live geometry");
    Check(first.GetDeviceMemoryUsage().ReservedHeapBytes >= sizeof(DeviceDraw), "device memory is not reported");
    std::set<std::array<float, 4>> colors;
    for (const auto state : {DeviceState::Stopped, DeviceState::Running, DeviceState::NeedsFuel,
                            DeviceState::WaitingForEnvironment, DeviceState::OutsideActiveArea, DeviceState::WaitingForCapacity}) {
        devices[0].State = state;
        BuildDeviceFrame(devices, camera, {160, 120}, second);
        colors.insert(second.DeviceDraws.front().Color);
    }
    Check(colors.size() == 6, "device states do not have distinct colors");
    BuildDeviceFrame({}, camera, {160, 120}, first);
    Check(first.DeviceDraws.empty(), "empty publication retained device geometry");
}

void PublisherIsolation() {
    GameSession session;
    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array cost{items::ItemAmount{items::ItemId::Stone, 4}, items::ItemAmount{items::ItemId::CopperOre, 2},
                          items::ItemAmount{items::ItemId::Coal, 2}};
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), cost) == items::Result::Applied,
          "could not fund device presentation fixture");
    Check(session.Execute(NativeCaller::Driver, api::PlaceDevice{{20, 20}}).Applied(), "could not place fixture device");
    const auto device = session.GetDevices().Snapshot().front();
    GameCamera camera;
    camera.SetFramebufferSize({160, 120});
    camera.GetCamera().SetPosition({20, 20});
    GamePresentation firstPresentation, secondPresentation;
    auto publishFirst = firstPresentation.CreateFramePublisher(session, camera);
    auto publishSecond = secondPresentation.CreateFramePublisher(session, camera);
    WorldGridFrame first, second;
    publishFirst(first);
    publishSecond(second);
    Check(first.DeviceDraws.size() == 1 && second.DeviceDraws.size() == 1, "committed device was not published");
    const auto stopped = *firstPresentation.PublishedGameplay();
    Check(stopped.Stamp == session.CurrentCommit() && stopped.Devices.size() == 1 &&
          stopped.Devices.front().State == DeviceState::Stopped && stopped.Inventories.size() == 2,
          "device, inventory and terrain publication did not share a commit");
    const auto fuel = std::find_if(stopped.Inventories.begin(), stopped.Inventories.end(),
        [&](const auto &value) { return value.DeviceId == device.Ref.Id; });
    Check(fuel != stopped.Inventories.end() && fuel->Quantity == 0 && fuel->Capacity == 64,
          "committed device fuel capacity missing");

    Check(session.Execute(NativeCaller::Driver, api::SupplyDevice{device.Ref, 2}).Applied(), "could not fuel fixture device");
    const auto fueled = session.GetDevices().Find(device.Ref.Id)->Value;
    Check(session.Execute(NativeCaller::Driver, api::SetDeviceEnabled{fueled.Ref, true}).Applied(), "could not enable fixture device");
    session.BeginFrame();
    Check(session.Control(NativeCaller::Driver, api::StepSimulation{}).Succeeded(), "could not step fixture device");
    session.Tick(1.f / 60, {});
    Check(firstPresentation.PublishedGameplay()->Stamp == stopped.Stamp &&
          secondPresentation.PublishedGameplay()->Devices.front().State == DeviceState::Stopped,
          "live actions mutated a published UI observation");
    publishSecond(second);
    const auto running = *secondPresentation.PublishedGameplay();
    Check(running.Stamp == session.CurrentCommit() && running.Stamp.Revision > stopped.Stamp.Revision &&
          running.Devices.front().State == DeviceState::Running && running.Devices.front().Fuel == 1 &&
          second.DeviceDraws.front().Color != first.DeviceDraws.front().Color,
          "running device geometry and observation were published from different states");
    Check(running.Processes.size() == 1 && running.Processes.front().InstanceId == running.Devices.front().InstanceId &&
          running.Processes.front().Carrier == api::CarrierKind::Device && !running.Processes.front().Suspended &&
          running.Processes.front().ActiveSteps > 0 && running.Processes.front().RemainingSteps > 0 &&
          running.Processes.front().WaitingFor == "result",
          "device program wait was not published with its device");
    Check(running.FieldSources.size() == 1 && running.FieldSources.front().DeviceId == device.Ref.Id &&
          running.FieldSources.front().OwnerInstance == running.Devices.front().InstanceId &&
          running.FieldSources.front().Contributing && !running.FieldSources.front().Suspended &&
          running.FieldSources.front().RadiusCells == 16 &&
          running.FieldSources.front().AccelerationCellsPerSecondSquared == std::array<double, 2>{3600, 0},
          "field source geometry or ownership was not published with its device");
    Check(firstPresentation.PublishedGameplay()->Stamp == stopped.Stamp &&
          firstPresentation.PublishedGameplay()->FieldSources.empty(), "second consumer mutated first consumer gameplay");

    Check(session.Execute(NativeCaller::Driver, api::RemoveDevice{running.Devices.front().Ref}).Applied(), "could not remove fixture device");
    Check(secondPresentation.PublishedGameplay()->Stamp == running.Stamp && second.DeviceDraws.size() == 1,
          "device removal changed UI values before frame publication");
    publishSecond(second);
    const auto &removed = *secondPresentation.PublishedGameplay();
    Check(first.DeviceDraws.size() == 1 && second.DeviceDraws.empty() && removed.Stamp == session.CurrentCommit() &&
          removed.Devices.empty() && removed.Processes.empty() && removed.FieldSources.empty() && removed.Inventories.size() == 1,
          "device removal retained published gameplay or mutated another frame");
    Check(stopped.Devices.front().State == DeviceState::Stopped && running.FieldSources.size() == 1 &&
          firstPresentation.PublishedGameplay()->Stamp == stopped.Stamp, "retained observations alias new commit values");
    Check(session.Execute(NativeCaller::Driver, api::PlaceDevice{{20, 20}}).Applied(), "could not replace fixture device");
    firstPresentation.DisplayMode = GridDisplayMode::ForceField;
    publishFirst(first);
    Check(first.DeviceDraws.empty() && firstPresentation.PublishedGameplay()->Devices.size() == 1 &&
          firstPresentation.PublishedGameplay()->Stamp == session.CurrentCommit(),
          "F1 retained stale geometry or lost committed device observations");
}

void DeviceValidation() {
    WorldGridFrame frame;
    DeviceDraw draw;
    draw.CenterRadius = {20, 20, 16, 16};
    draw.OutputCenterRadius = {32, 20, 4, 6};
    draw.Color = {1, 0, 0, 1};
    frame.DeviceDraws.push_back(draw);
    ValidateWorldGridFrame(frame.Read(), 4, 100, 100);
    frame.DeviceDraws.front().OutputCenterRadius[2] = 0;
    bool rejected = false;
    try { ValidateWorldGridFrame(frame.Read(), 4, 100, 100); }
    catch (const std::invalid_argument &) { rejected = true; }
    Check(rejected, "zero-sized device output geometry was accepted");
}
void FieldDomainPublicationWithoutWind() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("pixel-field-view-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        GameSession session;
        auto &world = testing::SessionAccess::World(session);
        WorldStreamSettings settings;
        settings.Directory = directory;
        settings.WindowEdge = 256;
        settings.SurfaceY = 10000;
        world.EnableStreaming(settings, {0, 0, 256, 256});
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(130, 100));
        world.DisableWind();
        const auto installed = world.InstallFieldSource(999, {WorldPosition::FromAbsolute(130, 100), 8, {3600, 0}, 20});
        Check(installed.has_value(), "domain publication source failed");
        auto view = session.CreateReadView();
        session.RefreshReadView(*view, {112, 80, 40, 40});
        WorldGridFrame direct, published;
        direct.SetRegion({112, 80}, 40, 40);
        published.SetRegion({112, 80}, 40, 40);
        BuildForceFieldFrame(world, direct);
        BuildForceFieldFrame(*view, published);
        Check(view->GetFieldVelocity(130, 100).X == 60, "initial field projection absent");
        const auto before = published.Cells()[20 * 40 + 18];
        const auto fieldRevision = world.GetEnvironment().GetFieldSources().Revision();
        const auto windRevision = world.GetEnvironment().GetWindSimulation().GetRevision();
        // Same resident region and visible bounds; only the simulation domain moves.
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromAbsolute(260, 128));
        session.RefreshReadView(*view, {112, 80, 40, 40});
        Check(world.GetSimulationBounds().X == 128 && world.GetForce(130, 100).X == 0 &&
              world.GetEnvironment().GetWindSimulation().GetRevision() == windRevision,
              "domain fixture did not isolate source activity from wind");
        Check(view->GetFieldVelocity(130, 100).X == 0, "committed field retained source outside the complete domain");
        BuildForceFieldFrame(world, direct);
        BuildForceFieldFrame(*view, published);
        Check(direct.Cells()[20 * 40 + 18] != before && published.Cells()[20 * 40 + 18] == direct.Cells()[20 * 40 + 18],
              "F1 cache retained source after domain-only change");
        Check(world.GetEnvironment().GetFieldSources().Revision() > fieldRevision &&
              world.GetEnvironment().GetFieldSources().Sources()[0].Remaining == 20,
              "domain publication aged or failed to invalidate source state");
        // Region alignment may keep a small window unchanged even though the
        // focus is outside its central quarter. Do not publish a phantom move.
        settings.WindowEdge = 64;
        world.EnableStreaming(settings, {0, 0, 64, 64});
        world.DisableWind();
        Check(world.InstallFieldSource(999, {WorldPosition::FromAbsolute(32, 32), 8, {3600, 0}, 20}).has_value(),
              "small domain source failed");
        const auto stableRevision = world.GetEnvironment().GetFieldSources().Revision();
        world.SetSimulationFocus(WorldPosition::FromAbsolute(48, 32));
        for (int i = 0; i < 3; ++i) world.UpdateStreaming();
        Check(world.GetSimulationBounds() == WorldRect{0, 0, 64, 64} &&
              world.GetEnvironment().GetFieldSources().Revision() == stableRevision,
              "unchanged aligned domain invalidated source publication");
    }
    std::filesystem::remove_all(directory);
}

void FieldResidencyPublicationWithoutWind() {
    GameSession session;
    auto &world = testing::SessionAccess::World(session);
    auto &map = testing::WorldAccess::Map(world);
    map.ResetInfinite();
    map.Insert(std::make_unique<WorldChunk>(CellCoord{0, 0}));
    map.Insert(std::make_unique<WorldChunk>(CellCoord{1, 0}));
    const int64_t edge = storage::ChunkEdge;
    testing::WorldAccess::SimulationBounds(world) = {edge - 24, 0, 64, 64};
    world.DisableWind();
    world.FillBaseForceRegion(world.GetSimulationBounds(), {});
    Check(world.InstallFieldSource(999, {WorldPosition::FromAbsolute(double(edge) - .5, 20.5), 4, {3600, 0}, 10}).has_value(),
          "residency publication source failed");
    auto view = session.CreateReadView();
    session.RefreshReadView(*view, {edge, 0, 32, 32});
    WorldGridFrame direct, published;
    direct.SetRegion({edge, 0}, 32, 32);
    published.SetRegion({edge, 0}, 32, 32);
    BuildForceFieldFrame(world, direct);
    BuildForceFieldFrame(*view, published);
    const auto before = direct.Cells()[20 * 32];
    const auto regionVersion = world.GetMap().RegionVersion({edge, 20});
    for (int cycle = 0; cycle < 2; ++cycle) {
        auto unloaded = testing::WorldAccess::Map(world).Remove({0, 0});
        testing::SessionAccess::PublishFixtureChanges(session);
        Check(view->GetFieldVelocity(edge, 20).X == 0 && world.GetMap().RegionVersion({edge, 20}) == regionVersion,
              "neighbor view retained force after center unloaded");
        BuildForceFieldFrame(world, direct);
        BuildForceFieldFrame(*view, published);
        Check(direct.Cells()[20 * 32] != before && published.Cells()[20 * 32] == direct.Cells()[20 * 32],
              "direct or committed F1 cache retained a missing-center source");
        testing::WorldAccess::Map(world).Insert(std::move(unloaded));
        testing::SessionAccess::PublishFixtureChanges(session);
        Check(view->GetFieldVelocity(edge, 20).X == 60, "center reload did not refresh neighbor view");
        BuildForceFieldFrame(world, direct);
        BuildForceFieldFrame(*view, published);
        Check(direct.Cells()[20 * 32] == before && published.Cells()[20 * 32] == before,
              "center reload did not refresh both F1 caches");
    }
    const auto revision = world.GetEnvironment().GetFieldSources().Revision();
    map.Insert(std::make_unique<WorldChunk>(CellCoord{2, 0}));
    Check(world.GetEnvironment().GetFieldSources().Revision() == revision,
          "unrelated residency invalidated stable sources");
}

} // namespace
int main() {
    try {
        DeviceProjection();
        PublisherIsolation();
        DeviceValidation();
        FieldDomainPublicationWithoutWind();
        FieldResidencyPublicationWithoutWind();
        std::cout << "Device snapshot and rendering geometry passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
