#pragma once
#include "Runtime/Gameplay/GameAPI/CommitStamp.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include "Runtime/Simulation/World/World.h"
#include "Runtime/Gameplay/Actors/Actors.h"
#include "Runtime/Gameplay/Devices/Devices.h"
#include "Runtime/Gameplay/GameAPI/GameplayObservations.h"

namespace pixel {
// A bounded view of the latest complete commit, sharing the world's read slot.
// ReadState borrows until the next refresh; retain ReadBuffers() when a consumer
// (for example an asynchronous upload) needs the world version for longer.
class CommittedWorldView {
  public:
    bool IsValid() const { return CommittedAt.Revision != 0 && Buffer && ReadState().IsValid(); }
    const api::CommitStamp &Stamp() const { return CommittedAt; }
    WorldBuffers::ReadLease ReadBuffers() const { return Buffer; }
    WorldMapView ReadState() const { return Buffer ? Buffer->Read(RequestedBounds) : WorldMapView{}; }
    WorldRect Bounds() const { return Buffer ? RequestedBounds : WorldRect{}; }
    std::optional<CellState> TryGetCell(int64_t x, int64_t y) const { return Buffer && RequestedBounds.Contains({x, y}) ? Buffer->GetMap().TryGet({x, y}) : std::nullopt; }
    WindVelocity SampleWind(CellCoord cell) const { const auto material = TryGetCell(cell.X, cell.Y); return material && material->Type == CellTypeId::Air ? Buffer->SampleWind(cell) : WindVelocity{}; }
    WindVelocity GetFieldVelocity(int64_t x, int64_t y) const { return TryGetCell(x, y) ? Buffer->FieldVelocity({x, y}) : WindVelocity{}; }
    // base must come from this view's validated TryForceRow.
    WindVelocity GetFieldVelocity(int64_t x, int64_t y, ForceVector base) const { return Buffer->FieldVelocity({x, y}, base); }
    uint64_t WindRevision() const { return WindVersion; }
    uint64_t FieldSourceRevision() const { return FieldSourceVersion; }
    uint32_t GetWindEndRow() const { return WindBounds.Height; }
    const std::optional<ActorSnapshot> &GetPlayer() const { return Player; }
    const std::vector<ActorSnapshot> &GetActors() const { return ActorValues; }
    const std::vector<FormulaProjectile> &Projectiles() const { return Bolts; }
    const std::vector<DeviceSnapshot> &GetDevices() const { return DeviceValues; }
    const api::GameplayObservation &Gameplay() const { return GameplayValues; }
    double SimulationTime() const { return CommittedAt.Tick / 60.0; }
    bool IsFinite() const { return Finite; }
    uint64_t ReservedBytes() const;
    static uint64_t CapacityFor(WorldRect bounds);

  private:
    friend class GameSession;
    WorldRect RequestedBounds, WindBounds;
    WorldBuffers::ReadLease Buffer;
    std::optional<ActorSnapshot> Player;
    std::vector<ActorSnapshot> ActorValues;
    std::vector<FormulaProjectile> Bolts;
    std::vector<DeviceSnapshot> DeviceValues;
    api::GameplayObservation GameplayValues;
    api::CommitStamp CommittedAt;
    uint64_t WindVersion = 0, FieldSourceVersion = 0;
    bool Finite = false;
    void Update(const World &world, const Actors &actors, const FormulaBridge &formulas, const Devices &devices,
                const api::GameplayObservation &gameplay, api::CommitStamp stamp);
};
} // namespace pixel
