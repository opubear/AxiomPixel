#include "Runtime/Gameplay/GameSession/GameSession.h"

namespace pixel {
namespace {
constexpr int64_t FieldRadius = 16;
const formula::process::ProgramArtifact &ForceEmitterProgram() {
    static const auto program = [] {
        auto compiled = formula::process::Compile("device/force-emitter/3",
            "formula 2; run field(cells(" + std::to_string(FieldRadius) + "), acceleration(3600), acceleration(0), ticks(30));");
        if (const auto *error = std::get_if<formula::process::Diagnostic>(&compiled))
            throw std::logic_error(error->Message);
        return std::get<formula::process::ProgramArtifact>(std::move(compiled));
    }();
    return program;
}
WorldPosition Origin(const DeviceSnapshot &device) {
    const CellCoord cell{device.X + 1, device.Y + 1};
    const auto chunk = ChunkOf(cell);
    return {chunk, {double(cell.X - chunk.X * storage::ChunkEdge), double(cell.Y - chunk.Y * storage::ChunkEdge)}};
}
} // namespace

DeviceState GameSession::DeviceEnvironment(const DeviceSnapshot &device) const {
    const auto bounds = GameWorld.GetSimulationBounds();
    // The complete output disk, as well as the fixture, must be in this window.
    if (!bounds.Contains({device.X + 1 - FieldRadius, device.Y + 1 - FieldRadius}) ||
        !bounds.Contains({device.X + 1 + FieldRadius, device.Y + 1 + FieldRadius}))
        return DeviceState::OutsideActiveArea;
    for (uint32_t y = 0; y < DeviceHeight; ++y)
        for (uint32_t x = 0; x < DeviceWidth; ++x) {
            const auto cell = GameWorld.TryGetCell(device.X + x, device.Y + y);
            if (!cell) return DeviceState::OutsideActiveArea;
            if (cell->Type != CellTypeId::Air) return DeviceState::WaitingForEnvironment;
        }
    return DeviceState::WaitingForCapacity;
}

std::vector<api::DeviceChanged> GameSession::SynchronizeDeviceActivity() {
    // Revalidate durable carrier identity before allowing any saved continuation
    // or owned field to become active again. Version changes from supply/UI do
    // not replace the device; its non-reused ID and instance do.
    for (const auto &process : Formulas.InspectProcesses()) {
        if (process.Carrier != ProcessCarrierKind::Device) continue;
        const auto *device = Machines.Find(process.Subject);
        if (!device || device->Value.Instance != process.State.Id || !device->Value.Enabled
            || !Characters.Find(ActorId{device->Value.Owner}))
            Formulas.CancelCarrier(GameWorld, ProcessCarrierKind::Device, process.Subject);
    }
    std::vector<api::DeviceChanged> changes;
    for (auto device : Machines.Snapshot()) {
        if (!device.Enabled) continue;
        const auto before = device;
        if (!Characters.Find(ActorId{device.Owner})) {
            device.Enabled = false;
            device.Instance = 0;
            device.State = DeviceState::Stopped;
        } else {
            const auto environment = DeviceEnvironment(device);
            if (environment == DeviceState::WaitingForEnvironment) {
                Formulas.CancelCarrier(GameWorld, ProcessCarrierKind::Device, device.Ref.Id);
                device.Instance = 0;
            }
            device.State = environment;
            if (environment == DeviceState::WaitingForCapacity)
                device.State = device.Instance ? DeviceState::Running
                    : device.Fuel ? DeviceState::WaitingForCapacity : DeviceState::NeedsFuel;
        }
        if (device != before) {
            Machines.Update(device);
            changes.push_back({Machines.Find(device.Ref.Id)->Value, api::DeviceEvent::StateChanged});
        }
    }
    return changes;
}

void GameSession::StepDevices(std::vector<FormulaEffectRecord> &effects, std::vector<api::DeviceChanged> &changes) {
    for (auto device : Machines.Snapshot()) {
        const auto before = device;
        if (!device.Enabled) continue;
        const auto ready = DeviceEnvironment(device);
        if (ready == DeviceState::OutsideActiveArea) {
            device.State = ready;
        } else if (ready != DeviceState::WaitingForCapacity) {
            Formulas.CancelCarrier(GameWorld, ProcessCarrierKind::Device, device.Ref.Id);
            device.Instance = 0;
            device.State = ready;
        } else if (Formulas.HasCarrier(ProcessCarrierKind::Device, device.Ref.Id)) {
            device.State = DeviceState::Running;
        } else {
            device.Instance = 0;
            device.State = device.Fuel ? DeviceState::WaitingForCapacity : DeviceState::NeedsFuel;
            if (device.Fuel && GameWorld.GetEnvironment().GetFieldSources().HasCapacity()) {
                const auto fuel = Machines.Find(device.Ref.Id)->FuelContainer;
                const std::array cost{items::ItemAmount{items::ItemId::Coal, 1}};
                const auto payment = Inventory.Reserve(Inventory.Reference(fuel), cost);
                if (payment.Status != items::Result::ReservationLimit) {
                    if (payment.Status != items::Result::Applied) throw std::logic_error("device fuel snapshot diverged");
                    ProcessCarrier carrier;
                    carrier.Kind = ProcessCarrierKind::Device;
                    carrier.Subject = device.Ref.Id;
                    carrier.Origin = Origin(device);
                    carrier.Scope = GameWorld.GetSimulationBounds();
                    carrier.MaximumRadius = FieldRadius;
                    carrier.ActivityRadius = FieldRadius;
                    carrier.Credits = 1;
                    carrier.Grants = formula::process::Mask(formula::process::Capability::Field);
                    if (Formulas.CastProcess(GameWorld, ForceEmitterProgram(), {3, 3}, carrier)) {
                        if (Formulas.LastEffects().size() != 1 || Inventory.Consume(payment.Id) != items::Result::Applied)
                            throw std::logic_error("preflighted force emitter installation failed");
                        effects.insert(effects.end(), Formulas.LastEffects().begin(), Formulas.LastEffects().end());
                        --device.Fuel;
                        if (device.Cycles == UINT64_MAX) throw std::overflow_error("device cycle counter exhausted");
                        ++device.Cycles;
                        device.Instance = Formulas.LastActivationId();
                        device.State = DeviceState::Running;
                    } else if (Inventory.Release(payment.Id) != items::Result::Applied) {
                        throw std::logic_error("device fuel release failed");
                    }
                }
            }
        }
        if (device != before) {
            Machines.Update(device);
            changes.push_back({Machines.Find(device.Ref.Id)->Value, api::DeviceEvent::StateChanged});
        }
    }
}

std::vector<api::DeviceChanged> GameSession::StopDevices() {
    std::vector<api::DeviceChanged> changes;
    for (auto device : Machines.Snapshot()) {
        if (!device.Enabled) continue;
        device.Enabled = false;
        device.Instance = 0;
        device.State = DeviceState::Stopped;
        Machines.Update(device);
        changes.push_back({Machines.Find(device.Ref.Id)->Value, api::DeviceEvent::Enabled});
    }
    return changes;
}
} // namespace pixel
