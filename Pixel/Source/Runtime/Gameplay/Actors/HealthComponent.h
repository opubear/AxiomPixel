#pragma once
#include "Component.h"
#include "HealthState.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel {
class HealthComponent final : public Component {
  public:
    HealthComponent(Actor &owner, HealthState state) : Component(owner), State(state) {
        if (!std::isfinite(state.Current) || !std::isfinite(state.Maximum) || state.Current < 0
            || state.Maximum < 1 || state.Maximum > 1e6f || state.Current > state.Maximum || state.InvulnerableTicks > 600)
            throw std::invalid_argument("invalid actor health");
        State.Respawn.Normalize();
    }
    const HealthState &GetState() const { return State; }
    bool Damage(float amount) {
        if (!std::isfinite(amount) || amount < 0) throw std::invalid_argument("invalid actor damage");
        if (!amount || !State.Current || State.InvulnerableTicks) return false;
        State.Current = std::max(0.f, State.Current - amount);
        return true;
    }
    void Step() { if (State.InvulnerableTicks) --State.InvulnerableTicks; }
    void Protect(uint32_t ticks) { State.InvulnerableTicks = ticks; }
    void Revive() { State.Current = State.Maximum; State.InvulnerableTicks = 120; }
  private:
    HealthState State;
};
} // namespace pixel
