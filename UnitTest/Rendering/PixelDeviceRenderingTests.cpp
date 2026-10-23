#include "../Support/TestAccess.h"
#include "PixelRender/DeviceFrame.h"
#include "PixelRender/GamePresentation.h"
#include <iostream>
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
                            DeviceState::WaitingForEnvironment, DeviceState::OutsideActiveArea, DeviceState::WaitingForCapacity,
                            DeviceState::OutputUnresolved}) {
        devices[0].State = state;
        BuildDeviceFrame(devices, camera, {160, 120}, second);
        colors.insert(second.DeviceDraws.front().Color);
    }
    Check(colors.size() == 7, "device states do not have distinct colors");
    BuildDeviceFrame({}, camera, {160, 120}, first);
    Check(first.DeviceDraws.empty(), "empty publication retained device geometry");
}

void PublisherIsolation() {
    GameSession session;
    auto &inventory = testing::SessionAccess::Inventory(session);
    const std::array cost{items::ItemAmount{items::ItemId::Stone, 4}, items::ItemAmount{items::ItemId::CopperOre, 2}};
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), cost) == items::Result::Applied,
          "could not fund device presentation fixture");
    Check(session.Execute(NativeCaller::Driver, api::PlaceDevice{{20, 20}}).Applied(), "could not place fixture device");
    const auto device = session.GetDevices().Snapshot().front();
    GameCamera camera;
    camera.SetFramebufferSize({160, 120});
    camera.GetCamera().SetPosition({20, 20});
    GamePresentation presentation;
    auto publishFirst = presentation.CreateFramePublisher(session, camera);
    auto publishSecond = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame first, second;
    publishFirst(first);
    publishSecond(second);
    Check(first.DeviceDraws.size() == 1 && second.DeviceDraws.size() == 1, "committed device was not published");
    Check(session.Execute(NativeCaller::Driver, api::RemoveDevice{device.Ref}).Applied(), "could not remove fixture device");
    publishSecond(second);
    Check(first.DeviceDraws.size() == 1 && second.DeviceDraws.empty(), "device removal mutated another published frame");
    Check(session.Execute(NativeCaller::Driver, api::PlaceDevice{{20, 20}}).Applied(), "could not replace fixture device");
    presentation.DisplayMode = GridDisplayMode::ForceField;
    publishFirst(first);
    Check(first.DeviceDraws.empty(), "F1 retained stale device geometry");
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
} // namespace
int main() {
    try {
        DeviceProjection();
        PublisherIsolation();
        DeviceValidation();
        std::cout << "Device snapshot and rendering geometry passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
