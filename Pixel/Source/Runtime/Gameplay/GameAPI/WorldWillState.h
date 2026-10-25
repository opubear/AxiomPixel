#pragma once
#include "Actions.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pixel::api {
enum class DecisionState {
    Thinking, Ready, Applied, Rejected, NoAction, TimedOut, StaleEpoch,
    ViewRevoked, ModelUnavailable, InvalidProposal, Closed, Interrupted, Cancelled
};

// A durable prayer identity is (world ID, Id). Transport request IDs, workers
// and observation leases are deliberately absent from this game-state record.
struct PrayerRecord {
    uint64_t Id = 0;
    CommitStamp ObservedAt, ResolvedAt;
    Rect Scope;
    DecisionState State = DecisionState::Thinking;
    std::string Explanation; // Model-derived text, never a fact or permission.
    std::optional<RequestKey> Action;
    FactRange Facts;
    uint64_t InstanceId = 0, EvidenceSequence = 0;
    Reason Rejection = Reason::None;
    std::string Prayer; // Player-authored intent, never a grant of authority.
};
struct WorldWillMemory {
    static constexpr size_t Capacity = 32, ExplanationBytes = 1024, PrayerBytes = 1024;
    uint64_t NextPrayer = 1, AidAccepted = 0;
    // Player choice only: enabling protection records autonomy, without a
    // punishment score or a model-controlled permission change.
    bool AutonomyChosen = false;
    std::vector<PrayerRecord> Records;
    uint64_t RetainedBytes() const;
};
// Player text is bounded UTF-8 without NUL or non-text control characters.
bool ValidPrayerText(std::string_view text);
} // namespace pixel::api
