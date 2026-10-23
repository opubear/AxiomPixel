#include "Persistence/WorldWillCodec.h"
#include "World/WorldStorageError.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;

void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

api::WorldWillMemory Fixture() {
    api::WorldWillMemory memory;
    memory.NextPrayer = 12;
    memory.AidAccepted = 1;
    memory.AutonomyChosen = true;
    for (const auto state : {api::DecisionState::Thinking, api::DecisionState::Applied,
        api::DecisionState::Rejected, api::DecisionState::NoAction, api::DecisionState::TimedOut,
        api::DecisionState::StaleEpoch, api::DecisionState::ViewRevoked, api::DecisionState::ModelUnavailable,
        api::DecisionState::InvalidProposal, api::DecisionState::Closed, api::DecisionState::Interrupted}) {
        api::PrayerRecord record;
        record.Id = memory.Records.size() + 1;
        record.ObservedAt = {"will-world", 5, 8, 13};
        record.Scope = {{-7, 11}, 32, 16};
        record.State = state;
        record.Explanation = "model \"decision\"\\line\nnext";
        if (state != api::DecisionState::Thinking) record.ResolvedAt = {"will-world", 5, 9, 17};
        if (state == api::DecisionState::Applied) {
            record.Action = api::RequestKey{5, 19, 23};
            record.Facts = {5, 29, 2};
            record.InstanceId = 31;
            record.EvidenceSequence = 37;
        } else if (state == api::DecisionState::Rejected) {
            record.Action = api::RequestKey{5, 19, 24};
            record.Rejection = api::Reason::PermissionDenied;
        } else if (state == api::DecisionState::Interrupted) {
            record.ResolvedAt.WorldEpoch = 6;
        }
        memory.Records.push_back(record);
    }
    return memory;
}

void RoundTrip() {
    const auto original = Fixture();
    const auto encoded = EncodeWorldWillState(original);
    const auto restored = DecodeWorldWillState(encoded);
    Check(restored.NextPrayer == 12 && restored.AidAccepted == 1 && restored.AutonomyChosen
        && restored.Records.size() == original.Records.size(), "world-will memory totals changed");
    for (size_t index = 0; index < restored.Records.size(); ++index) {
        const auto &record = restored.Records[index];
        const auto &expected = original.Records[index];
        Check(record.Id == expected.Id && record.ObservedAt == expected.ObservedAt
            && record.ResolvedAt == expected.ResolvedAt && record.Scope == expected.Scope && record.State == expected.State
            && record.Explanation == expected.Explanation && record.Action == expected.Action
            && record.Facts.WorldEpoch == expected.Facts.WorldEpoch && record.Facts.FirstSequence == expected.Facts.FirstSequence
            && record.Facts.Count == expected.Facts.Count && record.InstanceId == expected.InstanceId
            && record.EvidenceSequence == expected.EvidenceSequence && record.Rejection == expected.Rejection,
            "prayer identity, outcome, model text or action evidence changed");
    }
    Check(EncodeWorldWillState(restored) == encoded, "world-will checkpoint encoding is unstable");
    const auto empty = DecodeWorldWillState(EncodeWorldWillState({}));
    Check(empty.NextPrayer == 1 && !empty.AidAccepted && !empty.AutonomyChosen && empty.Records.empty(),
        "empty world-will memory changed");

    auto full = original;
    full.Records.clear();
    full.NextPrayer = 100;
    full.AidAccepted = 40;
    for (uint64_t id = 68; id < 100; ++id) {
        auto record = original.Records[3];
        record.Id = id;
        record.Explanation.assign(api::WorldWillMemory::ExplanationBytes, '\\');
        full.Records.push_back(record);
    }
    const auto bounded = DecodeWorldWillState(EncodeWorldWillState(full));
    Check(bounded.Records.size() == api::WorldWillMemory::Capacity && bounded.Records.front().Id == 68
        && bounded.Records.back().Id == 99 && bounded.AidAccepted == 40
        && bounded.Records.front().Explanation == full.Records.front().Explanation,
        "full bounded archive, retired aid count or escaped text changed");
}

std::string Replace(std::string input, std::string_view before, std::string_view after) {
    const auto position = input.find(before);
    Check(position != std::string::npos, "malformed world-will fixture token was not found");
    input.replace(position, before.size(), after);
    return input;
}

void ExpectFailure(std::string_view payload) {
    bool rejected = false;
    try { (void)DecodeWorldWillState(payload); }
    catch (const WorldStorageError &) { rejected = true; }
    Check(rejected, "malformed external world-will checkpoint was accepted");
}

void RejectMalformedArchive() {
    const auto encoded = EncodeWorldWillState(Fixture());
    ExpectFailure(Replace(encoded, "PixelWorldWill 1", "PixelWorldWill 2"));
    ExpectFailure(Replace(encoded, "\n12 1 1 11 \n", "\n0 1 1 11 \n"));
    ExpectFailure(Replace(encoded, "\n12 1 1 11 \n", "\n12 12 1 11 \n"));
    ExpectFailure(Replace(encoded, "\n12 1 1 11 \n", "\n12 0 1 11 \n"));
    ExpectFailure(Replace(encoded, "\n12 1 1 11 \n", "\n12 1 1 33 \n"));
    ExpectFailure(Replace(encoded, "\n12 1 1 11 \n", "\n12 1 1 -1 \n"));
    ExpectFailure(Replace(encoded, "\n12 1 1 11 \n", "\n12 1 2 11 \n"));
    ExpectFailure(Replace(encoded, "\n2 \"will-world\"", "\n1 \"will-world\""));
    ExpectFailure(Replace(encoded, "\n11 \"will-world\"", "\n12 \"will-world\""));
    ExpectFailure(Replace(encoded, "\"will-world\" 5 8 13", "\"will-world\" 0 8 13"));
    ExpectFailure(Replace(encoded, "\"will-world\" 5 9 17", "\"other-world\" 5 9 17"));
    ExpectFailure(Replace(encoded, "\"will-world\" 5 9 17", "\"will-world\" 6 9 17"));
    ExpectFailure(Replace(encoded, "\"will-world\" 5 9 17", "\"will-world\" 5 7 17"));
    ExpectFailure(Replace(encoded, "\"will-world\" 5 9 17", "\"will-world\" 5 9 12"));
    ExpectFailure(Replace(encoded, "\"\" 0 0 0", "\"will-world\" 5 9 17"));
    ExpectFailure(Replace(encoded, "-7 11 32 16", "-7 11 65 16"));
    ExpectFailure(Replace(encoded, " thinking ", " ready "));
    ExpectFailure(Replace(encoded, " thinking ", " unknown "));
    ExpectFailure(Replace(encoded, " applied ", " no_action "));
    ExpectFailure(Replace(encoded, "5 29 2 31 37 0", "5 29 2 31 0 0"));
    ExpectFailure(Replace(encoded, "5 29 2 31 37 0", "5 29 0 31 37 0"));
    ExpectFailure(Replace(encoded, "5 29 2 31 37 0", "5 29 2 0 37 0"));
    ExpectFailure(Replace(encoded, "5 29 2 31 37 0", "5 29 2 31 37 8"));
    ExpectFailure(Replace(encoded, "5 29 2 31 37 0", "5 18446744073709551615 2 31 37 0"));
    ExpectFailure(Replace(encoded, "1 5 19 23", "1 6 19 23"));
    ExpectFailure(Replace(encoded, "0 0 0 0 0 8 \n", "0 0 0 0 0 0 \n"));
    ExpectFailure(Replace(encoded, "0 0 0 0 0 8 \n", "0 0 0 0 0 99 \n"));
    ExpectFailure(Replace(encoded, "\"will-world\"", '"' + std::string(129, 'x') + '"'));
    ExpectFailure(Replace(encoded, "\"model", '"' + std::string(1025, 'x')));
    ExpectFailure(encoded + "unexpected");
    ExpectFailure(encoded.substr(0, encoded.size() / 2));
    ExpectFailure(std::string(128 * 1024 + 1, ' '));
}

void InternalInvalidDataKeepsItsError() {
    auto invalid = Fixture();
    invalid.Records[0].Explanation.assign(api::WorldWillMemory::ExplanationBytes + 1, 'x');
    bool programmerError = false;
    try { (void)EncodeWorldWillState(invalid); }
    catch (const std::invalid_argument &) { programmerError = true; }
    Check(programmerError, "invalid internal prayer DTO lost its programmer error");
}
} // namespace

int main() {
    try {
        RoundTrip();
        RejectMalformedArchive();
        InternalInvalidDataKeepsItsError();
        std::cout << "World-will checkpoint codec tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
