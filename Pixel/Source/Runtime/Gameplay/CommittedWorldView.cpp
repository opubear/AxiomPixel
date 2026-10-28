#include "CommittedWorldView.h"

namespace pixel {
uint64_t CommittedWorldView::CapacityFor(WorldRect) {
    return sizeof(CommittedWorldView)
        + Actors::MaximumCount * sizeof(ActorSnapshot)
        + 2 * FormulaBridge::MaximumEffects * sizeof(FormulaProjectile)
        + Devices::MaximumCount * sizeof(DeviceSnapshot) + api::GameplayObservation::MaximumRetainedBytes;
}

uint64_t CommittedWorldView::ReservedBytes() const {
    return sizeof(*this)
        + ActorValues.capacity() * sizeof(ActorSnapshot)
        + Bolts.capacity() * sizeof(FormulaProjectile) + DeviceValues.capacity() * sizeof(DeviceSnapshot)
        + GameplayValues.RetainedBytes() - sizeof(api::GameplayObservation);
}

void CommittedWorldView::Update(const World &world, const Actors &actors, const FormulaBridge &formulas, const Devices &devices,
                                const api::GameplayObservation &gameplay, api::CommitStamp stamp) {
    if (world.GetEnvironment().IsWaitingForBudget())
        throw std::logic_error("cannot publish a prepared environment step");
    Buffer = world.ReadBuffers();
    if (!Buffer) throw std::logic_error("world buffers have not been published");
    WindVersion = Buffer->GetWind().GetRevision();
    FieldSourceVersion = Buffer->GetSources().Revision();
    Player = actors.PlayerSnapshot();
    ActorValues = actors.Snapshot();
    Bolts = formulas.Projectiles();
    DeviceValues = devices.Snapshot();
    if (gameplay.Stamp != stamp) throw std::logic_error("gameplay projection has a different commit");
    GameplayValues = gameplay;
    WindBounds = Buffer->WindBounds();
    Finite = Buffer->GetMap().IsFinite();
    CommittedAt = std::move(stamp);
}
} // namespace pixel
