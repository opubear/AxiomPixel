#pragma once
#include "Body.h"
#include "Component.h"
#include "Runtime/Gameplay/GameAPI/ProgramRef.h"

namespace pixel {
struct FormulaLaunch {
    WorldPosition Center, Origin;
    std::array<double, 2> Direction;
    double AngleDegrees = 0;
};

// An equipped program reference, not a running instance. The session resolves
// the artifact and submits effects only when the actor explicitly activates it.
class FormulaComponent final : public Component {
  public:
    FormulaComponent(Actor &owner, api::ProgramRef program);
    api::ProgramRef GetProgram() const { return Program; }
    void Equip(api::ProgramRef program);
    static constexpr float LaunchClearance = 12.5f; // cm beyond the projectile's radius.

    FormulaLaunch Aim(WorldPosition target, float projectileRadius,
                      std::array<double, 2> fallbackDirection) const;
    static FormulaLaunch AimFromBody(const Body &body, WorldPosition target, float projectileRadius,
                                    std::array<double, 2> fallbackDirection);

  private:
    api::ProgramRef Program;
};
} // namespace pixel
