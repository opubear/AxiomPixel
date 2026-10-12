#pragma once
#include "WorldState.h"

namespace pixel {
// Position is the top-left corner in world cells, with positive Y downward.
class Character {
  public:
    static constexpr float Width = 2, Height = 4;
    float X = 0, Y = 0;
    bool Grounded = false;
    void Tick(float duration, WorldStateView world, int horizontal, bool jump);
    bool Occupies(int x, int y) const;

  private:
    float VerticalSpeed = 0;
    bool Collides(WorldStateView world, float x, float y) const;
    void MoveAxis(WorldStateView world, float distance, bool vertical);
};
} // namespace pixel
