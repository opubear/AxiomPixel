#pragma once
#include "Component.h"
#include "MonsterCatalog.h"
#include "MonsterState.h"
#include <optional>

namespace pixel {
class BodyComponent;
struct MonsterStep { bool Advanced = false, Strike = false, Expired = false; };
class MonsterComponent final : public Component {
  public:
    MonsterComponent(Actor &owner, const MonsterDefinition &definition, MonsterState state);
    const MonsterState &GetState() const { return State; }
    const MonsterDefinition &GetDefinition() const { return Definition; }
    MonsterStep Step(WorldMapView terrain, const std::optional<Body> &target);
    void Hit(bool died);
  private:
    // Catalog is immutable and owned by Actor/Actors for this component's lifetime.
    const MonsterDefinition &Definition;
    BodyComponent &Motion;
    MonsterState State;
    void Enter(MonsterAction action);
    bool CanStrike(WorldMapView terrain, const Body &body, const Body &target) const;
};
} // namespace pixel
