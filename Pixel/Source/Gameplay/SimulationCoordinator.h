#pragma once
#include "Actors/PlayerInput.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace pixel {
// Session policy and clock. Domains expose one step; only the host commits it
// after environment, character and program execution have all succeeded.
class SimulationCoordinator {
  public:
    static constexpr double StepSeconds = 1.0 / 60;
    bool IsPaused() const { return Paused; }
    bool HasPendingStep() const { return Pending; }
    uint64_t Tick() const { return CompletedTicks; }
    uint64_t Revision() const { return CommitRevision; }
    double Time() const { return CompletedTicks * StepSeconds; }
    double Remainder() const { return TimeRemainder; }
    void SetPaused(bool paused) { Paused = paused; }
    void RequestStep() { StepRequested = true; }
    void CancelStepRequest() { StepRequested = false; }
    void ClearInput() { Input = {}; }

    bool BeginFrame(float duration, PlayerInput input) {
        if (!std::isfinite(duration) || duration <= 0) return false;
        Input.Horizontal = std::clamp(input.Horizontal, -1, 1);
        Input.Jump |= input.Jump;
        if (Paused && !StepRequested) return false;
        if (StepRequested) {
            if (TimeRemainder < StepSeconds) TimeRemainder += StepSeconds;
        } else if (!Pending) {
            TimeRemainder = std::min(TimeRemainder + double(duration), .25);
        }
        return true;
    }

    bool HasStep() const { return TimeRemainder >= StepSeconds; }
    // Latch once, including across budget retries. New input is for the next step.
    PlayerInput BeginStep() {
        if (!HasStep()) throw std::logic_error("simulation step without time budget");
        if (!Pending) {
            StepInput = Input;
            Input.Jump = false;
            Pending = true;
        }
        return StepInput;
    }

    bool CommitStep() {
        if (!Pending || !HasStep()) throw std::logic_error("simulation commit without pending step");
        if (CompletedTicks == UINT64_MAX) throw std::overflow_error("simulation tick exhausted");
        ++CompletedTicks;
        TimeRemainder -= StepSeconds;
        Pending = false;
        CommitChange();
        const bool singleStep = StepRequested;
        StepRequested = false;
        return singleStep;
    }

    void CommitChange() {
        if (Pending) throw std::logic_error("observable commit during pending simulation step");
        if (CommitRevision == UINT64_MAX) throw std::overflow_error("commit revision exhausted");
        ++CommitRevision;
    }

    void Restore(double seconds, double remainder, bool deferred = false) {
        const double ticks = std::round(seconds / StepSeconds);
        if (!std::isfinite(seconds) || seconds < 0 || ticks >= double(UINT64_MAX)
            || !std::isfinite(remainder) || remainder < 0 || remainder > .25
            || (deferred && remainder < StepSeconds))
            throw std::invalid_argument("invalid fixed simulation clock");
        CompletedTicks = uint64_t(ticks);
        TimeRemainder = remainder;
        Pending = deferred;
        Input = StepInput = {};
        StepRequested = false;
    }

  private:
    uint64_t CompletedTicks = 0, CommitRevision = 1;
    double TimeRemainder = 0;
    bool Paused = true, StepRequested = false, Pending = false;
    PlayerInput Input{}, StepInput{};
};
} // namespace pixel
