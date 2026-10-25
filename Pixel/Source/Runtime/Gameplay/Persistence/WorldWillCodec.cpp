#include "WorldWillCodec.h"
#include "Runtime/Gameplay/Authority/Authority.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include <array>
#include <iomanip>
#include <locale>
#include <sstream>
#include <type_traits>

namespace pixel {
namespace {
constexpr size_t MaximumPayloadBytes = 256 * 1024;
constexpr std::array StateTags{
    "thinking", "ready", "applied", "rejected", "no_action", "timed_out", "stale_epoch",
    "view_revoked", "model_unavailable", "invalid_proposal", "closed", "interrupted", "cancelled"};
static_assert(StateTags.size() == size_t(api::DecisionState::Cancelled) + 1);

template<class T> void Read(std::istream &in, T &value) {
    in >> std::ws;
    if constexpr (std::is_unsigned_v<T>)
        if (in.peek() == '-') throw WorldStorageError("negative world-will identity/count");
    if (!(in >> value)) throw WorldStorageError("truncated or invalid world-will checkpoint");
}

template<class... T> void ReadAll(std::istream &in, T &...values) { (Read(in, values), ...); }
template<class... T> void WriteAll(std::ostream &out, const T &...values) { ((out << values << ' '), ...); }

std::string ReadText(std::istream &in, size_t maximum) {
    in >> std::ws;
    std::string value;
    if (in.peek() != '"' || !(in >> std::quoted(value)) || value.size() > maximum)
        throw WorldStorageError("invalid world-will checkpoint text");
    return value;
}

api::DecisionState ReadState(std::istream &in) {
    std::string tag;
    Read(in, tag);
    for (size_t index = 0; index < StateTags.size(); ++index)
        if (tag == StateTags[index]) return api::DecisionState(index);
    throw WorldStorageError("unknown world-will decision state");
}

void WriteStamp(std::ostream &out, const api::CommitStamp &stamp) {
    WriteAll(out, std::quoted(stamp.WorldId), stamp.WorldEpoch, stamp.Tick, stamp.Revision);
}

api::CommitStamp ReadStamp(std::istream &in) {
    api::CommitStamp stamp;
    stamp.WorldId = ReadText(in, 128);
    ReadAll(in, stamp.WorldEpoch, stamp.Tick, stamp.Revision);
    return stamp;
}

bool ValidStamp(const api::CommitStamp &stamp) {
    return !stamp.WorldId.empty() && stamp.WorldId.size() <= 128 && stamp.WorldEpoch
        && stamp.Revision && stamp.Revision != UINT64_MAX && stamp.Tick != UINT64_MAX;
}

bool EmptyFacts(const api::FactRange &facts) {
    return !facts.WorldEpoch && !facts.FirstSequence && !facts.Count;
}

void ValidateRecord(const api::PrayerRecord &record) {
    const auto state = record.State;
    if (uint32_t(state) >= StateTags.size() || !api::ValidPrayerText(record.Prayer)
        || record.Explanation.size() > api::WorldWillMemory::ExplanationBytes
        || !authority::Control::ValidRegion(record.Scope) || !ValidStamp(record.ObservedAt)
        || uint32_t(record.Rejection) > uint32_t(api::Reason::InsufficientItems))
        throw std::invalid_argument("invalid prayer state, text, scope or observation");
    if (state == api::DecisionState::Thinking || state == api::DecisionState::Ready) {
        if (record.ResolvedAt != api::CommitStamp{}) throw std::invalid_argument("pending prayer already resolved");
    } else if (!ValidStamp(record.ResolvedAt) || record.ResolvedAt.WorldId != record.ObservedAt.WorldId
        || (state != api::DecisionState::Interrupted && record.ResolvedAt.WorldEpoch != record.ObservedAt.WorldEpoch)
        || record.ResolvedAt.Tick < record.ObservedAt.Tick || record.ResolvedAt.Revision < record.ObservedAt.Revision) {
        throw std::invalid_argument("prayer resolution does not follow its observation");
    }

    const bool applied = state == api::DecisionState::Applied;
    const bool rejected = state == api::DecisionState::Rejected;
    if (bool(record.Action) != (applied || rejected))
        throw std::invalid_argument("prayer decision disagrees with its action reference");
    if (record.Action && (!record.Action->WorldEpoch || !record.Action->Caller || !record.Action->Sequence
        || record.Action->WorldEpoch != record.ObservedAt.WorldEpoch))
        throw std::invalid_argument("invalid prayer action identity");
    if (applied) {
        if (record.Rejection != api::Reason::None || !record.InstanceId || !record.EvidenceSequence
            || record.Facts.WorldEpoch != record.Action->WorldEpoch || !record.Facts.FirstSequence
            || !record.Facts.Count || record.Facts.Count - 1 > UINT64_MAX - record.Facts.FirstSequence)
            throw std::invalid_argument("applied prayer lacks committed action evidence");
    } else if (!EmptyFacts(record.Facts) || record.InstanceId || record.EvidenceSequence
        || ((record.Rejection != api::Reason::None) != rejected)) {
        throw std::invalid_argument("unapplied prayer contains action evidence or an invalid rejection");
    }
}

void Validate(const api::WorldWillMemory &state) {
    if (!state.NextPrayer || state.AidAccepted >= state.NextPrayer
        || state.Records.size() > api::WorldWillMemory::Capacity)
        throw std::invalid_argument("invalid world-will memory limits");
    uint64_t previous = 0, applied = 0;
    for (const auto &record : state.Records) {
        if (record.Id <= previous || record.Id >= state.NextPrayer)
            throw std::invalid_argument("prayer identities are not an ordered unique history");
        ValidateRecord(record);
        previous = record.Id;
        if (record.State == api::DecisionState::Applied) ++applied;
    }
    if (applied > state.AidAccepted) throw std::invalid_argument("aid count is below retained accepted prayers");
}

void WriteRecord(std::ostream &out, const api::PrayerRecord &record) {
    WriteAll(out, record.Id);
    WriteStamp(out, record.ObservedAt);
    WriteStamp(out, record.ResolvedAt);
    WriteAll(out, record.Scope.Origin.X, record.Scope.Origin.Y, record.Scope.Width, record.Scope.Height,
        StateTags[size_t(record.State)], std::quoted(record.Prayer), std::quoted(record.Explanation), bool(record.Action));
    if (record.Action) WriteAll(out, record.Action->WorldEpoch, record.Action->Caller, record.Action->Sequence);
    WriteAll(out, record.Facts.WorldEpoch, record.Facts.FirstSequence, record.Facts.Count,
        record.InstanceId, record.EvidenceSequence, uint32_t(record.Rejection));
    out << '\n';
}

api::PrayerRecord ReadRecord(std::istream &in, uint32_t version) {
    api::PrayerRecord record;
    Read(in, record.Id);
    record.ObservedAt = ReadStamp(in);
    record.ResolvedAt = ReadStamp(in);
    ReadAll(in, record.Scope.Origin.X, record.Scope.Origin.Y, record.Scope.Width, record.Scope.Height);
    record.State = ReadState(in);
    if (version == 1 && (record.State == api::DecisionState::Ready || record.State == api::DecisionState::Cancelled))
        throw WorldStorageError("world-will state is not supported by checkpoint version");
    if (version >= 2) record.Prayer = ReadText(in, api::WorldWillMemory::PrayerBytes);
    record.Explanation = ReadText(in, api::WorldWillMemory::ExplanationBytes);
    bool action;
    Read(in, action);
    if (action) {
        api::RequestKey request;
        ReadAll(in, request.WorldEpoch, request.Caller, request.Sequence);
        record.Action = request;
    }
    uint32_t rejection;
    ReadAll(in, record.Facts.WorldEpoch, record.Facts.FirstSequence, record.Facts.Count,
        record.InstanceId, record.EvidenceSequence, rejection);
    record.Rejection = api::Reason(rejection);
    return record;
}
} // namespace

std::string EncodeWorldWillState(const api::WorldWillMemory &state) {
    Validate(state);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "PixelWorldWill 2\n";
    WriteAll(out, state.NextPrayer, state.AidAccepted, state.AutonomyChosen, state.Records.size());
    out << '\n';
    for (const auto &record : state.Records) WriteRecord(out, record);
    return out.str();
}

api::WorldWillMemory DecodeWorldWillState(std::string_view payload) {
    if (payload.size() > MaximumPayloadBytes) throw WorldStorageError("oversized world-will checkpoint");
    std::istringstream in{std::string(payload)};
    in.imbue(std::locale::classic());
    std::string magic;
    uint32_t version;
    ReadAll(in, magic, version);
    if (magic != "PixelWorldWill" || (version != 1 && version != 2)) throw WorldStorageError("unsupported world-will checkpoint");

    api::WorldWillMemory state;
    size_t count;
    ReadAll(in, state.NextPrayer, state.AidAccepted, state.AutonomyChosen, count);
    if (count > api::WorldWillMemory::Capacity) throw WorldStorageError("world-will checkpoint capacity exceeded");
    state.Records.reserve(count);
    for (size_t index = 0; index < count; ++index) state.Records.push_back(ReadRecord(in, version));
    in >> std::ws;
    if (!in.eof()) throw WorldStorageError("unexpected world-will checkpoint data");
    try { Validate(state); }
    catch (const std::invalid_argument &error) { throw WorldStorageError(error.what()); }
    return state;
}
} // namespace pixel
