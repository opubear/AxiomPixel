#pragma once
#include "Runtime/Gameplay/GameAPI/CommitStamp.h"
#include "Runtime/Gameplay/FormulaBridge/FormulaBridge.h"
#include "Runtime/Simulation/World/World.h"
#include "Runtime/Gameplay/Actors/Actors.h"
#include "Runtime/Gameplay/Devices/Devices.h"
#include "Runtime/Gameplay/GameAPI/GameplayObservations.h"

namespace pixel {
// A host-owned, bounded projection refreshed only at complete commit boundaries.
// References are main-thread reads valid until the next commit. Frozen/paged
// observer leases belong to the later Observation API, not this live projection.
class CommittedWorldView {
  public:
    bool IsValid() const { return CommittedAt.Revision != 0 && Materials.Read().IsValid(); }
    const api::CommitStamp &Stamp() const { return CommittedAt; }
    WorldMapView ReadState() const { return Materials.Read(); }
    WorldRect Bounds() const { const auto v = ReadState(); return {v.Origin.X, v.Origin.Y, v.Width, v.Height}; }
    std::optional<CellState> TryGetCell(int64_t x, int64_t y) const { return Materials.TryGet({x, y}); }
    WindVelocity SampleWind(CellCoord cell) const { return Materials.TryGet(cell) ? Air.at(Index(cell)) : WindVelocity{}; }
    WindVelocity GetFieldVelocity(int64_t x, int64_t y) const { return Materials.TryGet({x, y}) ? Field.at(Index({x, y})) : WindVelocity{}; }
    uint64_t WindRevision() const { return WindVersion; }
    uint64_t FieldSourceRevision() const { return FieldSourceVersion; }
    uint32_t GetWindEndRow() const { return WindBounds.Height; }
    const std::optional<ActorSnapshot> &GetPlayer() const { return Player; }
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
    WorldSnapshot Materials;
    std::vector<WindVelocity> Air, Field;
    std::optional<ActorSnapshot> Player;
    std::vector<FormulaProjectile> Bolts;
    std::vector<DeviceSnapshot> DeviceValues;
    api::GameplayObservation GameplayValues;
    api::CommitStamp CommittedAt;
    uint64_t WindVersion = 0, FieldSourceVersion = 0;
    bool Finite = false;
    size_t Index(CellCoord cell) const;
    void Update(const World &world, const Actors &actors, const FormulaBridge &formulas, const Devices &devices,
                const api::GameplayObservation &gameplay, api::CommitStamp stamp);
};
} // namespace pixel
