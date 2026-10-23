#include "Persistence/KnowledgeCodec.h"
#include "Knowledge/Knowledge.h"
#include "World/WorldStorageError.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace pixel;
using namespace pixel::knowledge;

void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

api::DomainFact Fact(uint64_t sequence, api::FactCause cause, api::FactPayload payload) {
    return {{41, sequence}, {"knowledge \"world\"\\checkpoint", 41, sequence * 2, sequence * 3},
        std::move(cause), std::move(payload)};
}

SaveData Fixture() {
    Journal journal;
    journal.Record(Fact(1, api::RequestKey{41, 5, 7}, api::MaterialRemoved{{-12, 9}, 2, 0}));
    journal.Record(Fact(2, api::ProgramEffectCause{{41, 17}, {23, 5}, 19,
        api::RequestKey{41, 29, 31}, api::FactId{41, 1}},
        api::MaterialRemovedInDisk{{{-33, 44}, {0.125, 0.875}}, 5.25, 47, 11, 13}));
    journal.Record(Fact(3, api::DeviceCause{37, 41}, api::DeviceChanged{
        {{43, 7}, 53, -59, 61, true, DeviceState::WaitingForCapacity, 67, 71, 73}, api::DeviceEvent::Supplied}));
    journal.Record(Fact(4, api::AuthorityCause{79}, api::ForceObserved{83, {-89, 97}, {3600.5, -0.0625}}));
    journal.Record(Fact(5, api::RequestKey{41, 101, 103},
        api::AuthorityChanged{107, 109, {{-113, 127}, 131, 137}, true, false, true}));
    journal.Record(Fact(6, api::ProgramEffectCause{{41, 139}, {149, 151}, 157, {}, {}},
        api::InterventionStopped{{41, 163}, 167, api::InterventionStopReason::LeaseExpired}));
    journal.Record(Fact(7, api::RequestKey{41, 173, 179}, api::ProgramActivated{{181, 191}, 193}));
    return journal.Export();
}

void CompleteRoundTrip() {
    const auto original = Fixture();
    const auto payload = EncodeKnowledgeState(original);
    const auto restored = DecodeKnowledgeState(payload);
    Check(restored.Version == original.Version && restored.WorldId == original.WorldId
        && restored.NextSequence == original.NextSequence && restored.RuntimeFirstSequence == original.RuntimeFirstSequence
        && restored.LastSource == original.LastSource && restored.LastRevision == original.LastRevision
        && restored.LastTick == original.LastTick && restored.History.size() == 7
        && restored.Discoveries.size() == 1, "knowledge identity, watermarks or counts changed");
    for (size_t index = 0; index < restored.History.size(); ++index) {
        const auto &record = restored.History[index];
        const auto &expected = original.History[index];
        Check(record.Sequence == expected.Sequence && record.Source.Id == expected.Source.Id
            && record.Source.CommittedAt == expected.Source.CommittedAt,
            "history source identity or commit stamp changed");
    }
    const auto &history = restored.History;
    Check(std::get<api::RequestKey>(history[0].Source.Cause) == api::RequestKey{41, 5, 7},
        "request cause changed");
    const auto &program = std::get<api::ProgramEffectCause>(history[1].Source.Cause);
    Check(program.Instance == api::InstanceRef{41, 17} && program.Program == api::ProgramRef{23, 5}
        && program.EmissionSequence == 19 && program.ActivationRequest == api::RequestKey{41, 29, 31}
        && program.ParentFact == api::FactId{41, 1}, "program effect ancestry changed");
    const auto &deviceCause = std::get<api::DeviceCause>(history[2].Source.Cause);
    Check(deviceCause.DeviceId == 37 && deviceCause.InstanceId == 41, "device cause changed");
    Check(std::get<api::AuthorityCause>(history[3].Source.Cause).Version == 79, "authority cause changed");
    const auto &withoutParents = std::get<api::ProgramEffectCause>(history[5].Source.Cause);
    Check(withoutParents.Instance == api::InstanceRef{41, 139} && withoutParents.Program == api::ProgramRef{149, 151}
        && withoutParents.EmissionSequence == 157 && !withoutParents.ActivationRequest && !withoutParents.ParentFact,
        "absent program cause parents were invented");

    const auto &removed = std::get<api::MaterialRemoved>(history[0].Source.Payload);
    Check(removed.Target == api::Cell{-12, 9} && removed.OldMaterial == 2 && removed.NewMaterial == 0,
        "mining payload changed");
    const auto &disk = std::get<api::MaterialRemovedInDisk>(history[1].Source.Payload);
    Check(disk.Center.AnchorCell == api::Cell{-33, 44} && disk.Center.Offset == std::array<double, 2>{0.125, 0.875}
        && disk.RadiusCells == 5.25 && disk.Changed == 47 && disk.Blocked == 11 && disk.NotReady == 13,
        "mining disk summary changed");
    const auto &device = std::get<api::DeviceChanged>(history[2].Source.Payload);
    Check(device.Device == DeviceSnapshot{{43, 7}, 53, -59, 61, true, DeviceState::WaitingForCapacity, 67, 71, 73}
        && device.Event == api::DeviceEvent::Supplied, "device snapshot or event changed");
    const auto &force = std::get<api::ForceObserved>(history[3].Source.Payload);
    Check(force.ActorId == 83 && force.Location == api::Cell{-89, 97}
        && force.AccelerationCellsPerSecondSquared == std::array<double, 2>{3600.5, -0.0625},
        "force observation changed");
    const auto &authority = std::get<api::AuthorityChanged>(history[4].Source.Payload);
    Check(authority.Version == 107 && authority.DeviceId == 109 && authority.Region == api::Rect{{-113, 127}, 131, 137}
        && authority.Protected && !authority.ObservationAllowed && authority.InterventionAllowed,
        "authority history changed");
    const auto &stopped = std::get<api::InterventionStopped>(history[5].Source.Payload);
    Check(stopped.Instance == api::InstanceRef{41, 163} && stopped.Subject == 167
        && stopped.Reason == api::InterventionStopReason::LeaseExpired, "intervention termination changed");
    const auto &activated = std::get<api::ProgramActivated>(history[6].Source.Payload);
    Check(activated.Program == api::ProgramRef{181, 191} && activated.ActivationId == 193,
        "program activation history changed");
    const auto &discovery = restored.Discoveries.front();
    Check(discovery.OwnerActor == 83 && discovery.Location == api::Cell{-89, 97}
        && discovery.EvidenceSequence == 4 && discovery.Observed && !discovery.Analyzed
        && !discovery.CanCopy && !discovery.CanEdit, "discovery permissions or evidence changed");
    Check(EncodeKnowledgeState(restored) == payload, "knowledge checkpoint encoding is not stable");
    Check(EncodeKnowledgeState(DecodeKnowledgeState(EncodeKnowledgeState({}))) == EncodeKnowledgeState({}),
        "empty knowledge checkpoint cannot round trip");
}

void RetainedHistoryAndRuntimeBoundary() {
    Journal journal;
    journal.Record(Fact(1, api::RequestKey{41, 1, 1}, api::ForceObserved{1, {3, 4}, {0, 1}}));
    for (uint64_t sequence = 2; sequence <= HistoryCapacity + 2; ++sequence)
        journal.Record(Fact(sequence, api::RequestKey{41, 1, sequence}, api::MaterialRemoved{{0, 0}, 1, 0}));
    journal.BeginRuntime(41);
    journal.Record(Fact(1, api::RequestKey{41, 2, 1},
        api::InterventionStopped{{41, 5}, 2, api::InterventionStopReason::ControlRevoked}));
    const auto restored = DecodeKnowledgeState(EncodeKnowledgeState(journal.Export()));
    Check(restored.History.size() == HistoryCapacity && restored.History.front().Sequence == 4
        && restored.History.back().Sequence == 131 && restored.NextSequence == 132
        && restored.RuntimeFirstSequence == 131 && restored.LastSource == api::FactId{41, 1}
        && restored.LastRevision == 3 && restored.LastTick == 2,
        "bounded suffix or reused runtime boundary changed");
    Check(restored.Discoveries.front().EvidenceSequence == 1,
        "evicted discovery evidence was discarded or rebound");
}

std::string Replace(std::string input, std::string_view before, std::string_view after) {
    const auto position = input.find(before);
    Check(position != std::string::npos, "malformed checkpoint test fixture token was not found");
    input.replace(position, before.size(), after);
    return input;
}

void ExpectFailure(std::string_view payload) {
    bool rejected = false;
    try { (void)DecodeKnowledgeState(payload); }
    catch (const WorldStorageError &) { rejected = true; }
    Check(rejected, "malformed external knowledge checkpoint was accepted");
}

void MalformedExternalData() {
    const auto encoded = EncodeKnowledgeState(Fixture());
    ExpectFailure(Replace(encoded, "PixelKnowledge 1", "PixelKnowledge 2"));
    ExpectFailure(Replace(encoded, "PixelKnowledge", "UnknownFormat"));
    ExpectFailure(Replace(encoded, "\n1 \"", "\n2 \""));
    ExpectFailure(Replace(encoded, "\n7 1 \n", "\n129 1 \n"));
    ExpectFailure(Replace(encoded, "\n7 1 \n", "\n7 129 \n"));
    ExpectFailure(Replace(encoded, "\n7 1 \n", "\n-1 1 \n"));
    ExpectFailure(Replace(encoded, "request 41 5 7", "unknown 41 5 7"));
    ExpectFailure(Replace(encoded, "material_removed -12", "unknown -12"));
    ExpectFailure(Replace(encoded, "intervention_stopped 41 163 167 1", "intervention_stopped 41 163 167 9"));
    ExpectFailure(Replace(encoded, "device_changed 43 7 53 -59 61 1 5", "device_changed 43 7 53 -59 61 1 99"));
    ExpectFailure(Replace(encoded, "authority_changed 107 109 -113 127 131 137 1 0 1",
        "authority_changed 107 109 -113 127 131 137 2 0 1"));
    ExpectFailure(Replace(encoded, "3600.5", "nan"));
    ExpectFailure(Replace(encoded, "0.125", "1.125"));
    ExpectFailure(Replace(encoded, "\n1 41 1 ", "\n2 41 1 "));
    ExpectFailure(Replace(encoded, "\n7 1 \n", "\n6 1 \n"));
    ExpectFailure(Replace(encoded, "request 41 5 7", "request -41 5 7"));
    ExpectFailure(encoded + "unexpected");
    ExpectFailure(encoded.substr(0, encoded.size() / 2));
    ExpectFailure(std::string(1024 * 1024 + 1, ' '));
    const auto empty = EncodeKnowledgeState({});
    ExpectFailure(Replace(empty, "\"\"", '"' + std::string(129, 'x') + '"'));
}

void InternalInvalidDataKeepsItsError() {
    auto invalid = Fixture();
    invalid.NextSequence++;
    bool programmerError = false;
    try { (void)EncodeKnowledgeState(invalid); }
    catch (const std::invalid_argument &) { programmerError = true; }
    Check(programmerError, "invalid internal knowledge DTO did not retain its programmer error");

    invalid = Fixture();
    std::get<api::MaterialRemovedInDisk>(invalid.History[1].Source.Payload).RadiusCells =
        std::numeric_limits<double>::infinity();
    programmerError = false;
    try { (void)EncodeKnowledgeState(invalid); }
    catch (const std::invalid_argument &) { programmerError = true; }
    Check(programmerError, "nonfinite internal knowledge value was serialized");
}
} // namespace

int main() {
    try {
        CompleteRoundTrip();
        RetainedHistoryAndRuntimeBoundary();
        MalformedExternalData();
        InternalInvalidDataKeepsItsError();
        std::cout << "Knowledge checkpoint codec tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
