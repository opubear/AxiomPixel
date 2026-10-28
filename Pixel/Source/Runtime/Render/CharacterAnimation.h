#pragma once
#include "Assets/ArtAssets.h"
#include "Runtime/Gameplay/Actors/Actors.h"
#include "WorldGridFrame.h"

namespace pixel {
// Owned by one presentation consumer; advances only when committed simulation time advances.
class CharacterAnimation {
  public:
    void Update(const ActorSnapshot &actor, uint64_t worldIdentity, double simulationTime);
    void Apply(const CharacterAssets &assets, WorldGridFrame &frame) const;
    CharacterMotion Motion() const { return CurrentMotion; }
    bool FacingLeft() const { return Left; }
  private:
    uint64_t WorldIdentity = 0;
    ActorId Actor;
    WorldPosition Position;
    double LastTime = 0, ClipTime = 0;
    CharacterMotion CurrentMotion = CharacterMotion::Idle;
    bool Left = false, Initialized = false;
};
} // namespace pixel
