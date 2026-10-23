#include "Knowledge.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pixel::knowledge {
namespace {
bool Important(const api::FactPayload &payload) {
    if (std::holds_alternative<api::AuthorityChanged>(payload)
        || std::holds_alternative<api::InterventionStopped>(payload)) return true;
    if (std::holds_alternative<api::MaterialRemoved>(payload) || std::holds_alternative<api::ForceObserved>(payload)) return true;
    if (const auto *disk = std::get_if<api::MaterialRemovedInDisk>(&payload)) return disk->Changed != 0;
    const auto *device = std::get_if<api::DeviceChanged>(&payload);
    return device && (device->Event == api::DeviceEvent::Placed || device->Event == api::DeviceEvent::Removed
        || device->Event == api::DeviceEvent::Supplied || device->Event == api::DeviceEvent::Enabled);
}

bool SameDiscovery(const Discovery &entry, uint64_t actor, api::Cell location) {
    return entry.OwnerActor == actor && entry.Location == location;
}

uint64_t RecordBytes(const std::vector<HistoryRecord> &records) {
    uint64_t bytes = records.capacity() * sizeof(HistoryRecord);
    for (const auto &record : records) bytes += record.Source.CommittedAt.WorldId.capacity() + 1;
    return bytes;
}

void Validate(const SaveData &data) {
    if (data.Version != SchemaVersion || !data.NextSequence || data.NextSequence == UINT64_MAX
        || !data.RuntimeFirstSequence || data.RuntimeFirstSequence > data.NextSequence
        || data.WorldId.size() > 128 || data.History.size() > HistoryCapacity
        || data.Discoveries.size() > DiscoveryCapacity)
        throw std::invalid_argument("invalid knowledge save version or limits");
    if (data.History.size() != std::min<uint64_t>(data.NextSequence - 1, HistoryCapacity))
        throw std::invalid_argument("knowledge history must retain its complete bounded suffix");
    if ((!data.LastSource.WorldEpoch && (data.LastSource.Sequence || data.LastRevision || data.LastTick))
        || (bool(data.LastSource.Sequence) != bool(data.LastRevision))
        || (!data.LastSource.Sequence && data.LastTick)
        || (data.WorldId.empty() && (data.NextSequence != 1 || !data.Discoveries.empty() || data.LastSource.Sequence)))
        throw std::invalid_argument("invalid knowledge source watermark");

    uint64_t expected = data.NextSequence - data.History.size();
    uint64_t sourceSequence = 0, sourceRevision = 0, sourceTick = 0;
    for (const auto &entry : data.History) {
        const auto &source = entry.Source;
        if (entry.Sequence != expected++ || !Important(source.Payload)
            || !source.Id.Sequence || !source.Id.WorldEpoch || !source.CommittedAt.Revision
            || source.CommittedAt.WorldEpoch != source.Id.WorldEpoch
            || source.CommittedAt.WorldId != data.WorldId)
            throw std::invalid_argument("invalid important history source");
        if (entry.Sequence >= data.RuntimeFirstSequence) {
            if (source.Id.WorldEpoch != data.LastSource.WorldEpoch
                || source.Id.Sequence <= sourceSequence || source.Id.Sequence > data.LastSource.Sequence
                || source.CommittedAt.Revision < sourceRevision || source.CommittedAt.Revision > data.LastRevision
                || source.CommittedAt.Tick < sourceTick || source.CommittedAt.Tick > data.LastTick)
                throw std::invalid_argument("knowledge sources disagree with runtime consumption watermark");
            sourceSequence = source.Id.Sequence;
            sourceRevision = source.CommittedAt.Revision;
            sourceTick = source.CommittedAt.Tick;
        }
        if (const auto *observed = std::get_if<api::ForceObserved>(&source.Payload)) {
            if (!observed->ActorId || !std::isfinite(observed->AccelerationCellsPerSecondSquared[0])
                || !std::isfinite(observed->AccelerationCellsPerSecondSquared[1]))
                throw std::invalid_argument("invalid recorded force observation");
        }
    }
    for (size_t i = 0; i < data.Discoveries.size(); ++i) {
        const auto &entry = data.Discoveries[i];
        if (!entry.OwnerActor || !entry.EvidenceSequence || entry.EvidenceSequence >= data.NextSequence
            || !entry.Observed || entry.Analyzed || entry.CanCopy || entry.CanEdit)
            throw std::invalid_argument("unsupported discovery knowledge or evidence");
        for (size_t j = 0; j < i; ++j)
            if (SameDiscovery(data.Discoveries[j], entry.OwnerActor, entry.Location))
                throw std::invalid_argument("duplicate discovery in knowledge save");
        const auto evidence = std::find_if(data.History.begin(), data.History.end(),
            [&](const auto &record) { return record.Sequence == entry.EvidenceSequence; });
        // An evicted source stays an explicit unresolved citation, never a new fact.
        if (evidence != data.History.end()) {
            const auto *observed = std::get_if<api::ForceObserved>(&evidence->Source.Payload);
            if (!observed || !SameDiscovery(entry, observed->ActorId, observed->Location))
                throw std::invalid_argument("discovery evidence does not describe this discovery");
        }
    }
}
} // namespace

uint64_t Snapshot::RetainedBytes() const {
    return sizeof(*this) + Stamp.WorldId.capacity() + 1 + RecordBytes(History)
        + Discoveries.capacity() * sizeof(Discovery);
}

HistoryRead ReadHistory(const Snapshot &snapshot, uint64_t after, uint32_t limit) {
    HistoryRead result;
    result.FirstSequence = snapshot.FirstSequence;
    result.HighWatermark = snapshot.HighWatermark;
    result.NextSequence = after;
    if (!limit || after > result.HighWatermark) {
        result.Status = ReadStatus::InvalidRange;
    } else if (limit > HistoryCapacity) {
        result.Status = ReadStatus::LimitExceeded;
    } else if (after < result.HighWatermark && after + 1 < result.FirstSequence) {
        result.Status = ReadStatus::Gap;
    } else {
        for (const auto &entry : snapshot.History) {
            if (entry.Sequence <= after) continue;
            if (result.Records.size() == limit) { result.HasMore = true; break; }
            result.Records.push_back(entry);
            result.NextSequence = entry.Sequence;
        }
    }
    return result;
}

void Journal::Reset() { Data = {}; }

void Journal::BeginRuntime(uint64_t epoch) {
    if (!epoch) throw std::logic_error("knowledge runtime requires nonzero epoch");
    Data.LastSource = {epoch, 0};
    Data.RuntimeFirstSequence = Data.NextSequence;
    Data.LastRevision = Data.LastTick = 0;
}

bool Journal::CanDiscover(uint64_t actor, api::Cell location) const {
    return Data.Discoveries.size() < DiscoveryCapacity
        || std::any_of(Data.Discoveries.begin(), Data.Discoveries.end(),
            [&](const auto &entry) { return SameDiscovery(entry, actor, location); });
}

void Journal::Record(const api::DomainFact &fact) {
    const auto &stamp = fact.CommittedAt;
    if (!fact.Id.WorldEpoch || fact.Id.WorldEpoch != stamp.WorldEpoch || !fact.Id.Sequence
        || !stamp.Revision || stamp.WorldId.empty() || stamp.WorldId.size() > 128)
        throw std::logic_error("knowledge requires a committed fact identity");
    if (Data.WorldId.empty()) Data.WorldId = stamp.WorldId;
    if (!Data.LastSource.WorldEpoch) BeginRuntime(fact.Id.WorldEpoch);
    if (stamp.WorldId != Data.WorldId || fact.Id.WorldEpoch != Data.LastSource.WorldEpoch)
        throw std::logic_error("knowledge fact belongs to another world or runtime");
    if (fact.Id.Sequence <= Data.LastSource.Sequence) return;
    if (fact.Id.Sequence != Data.LastSource.Sequence + 1 || stamp.Revision < Data.LastRevision || stamp.Tick < Data.LastTick)
        throw std::logic_error("knowledge consumer missed or reordered a committed fact");
    Data.LastSource = fact.Id;
    Data.LastRevision = stamp.Revision;
    Data.LastTick = stamp.Tick;
    if (!Important(fact.Payload)) return;

    const auto *observed = std::get_if<api::ForceObserved>(&fact.Payload);
    if (observed) {
        if (!observed->ActorId || !std::isfinite(observed->AccelerationCellsPerSecondSquared[0])
            || !std::isfinite(observed->AccelerationCellsPerSecondSquared[1]))
            throw std::logic_error("invalid committed observation");
        if (std::any_of(Data.Discoveries.begin(), Data.Discoveries.end(), [&](const auto &entry) {
            return SameDiscovery(entry, observed->ActorId, observed->Location);
        })) return;
        if (!CanDiscover(observed->ActorId, observed->Location))
            throw std::logic_error("discovery capacity was not checked before commit");
    }

    if (Data.NextSequence == UINT64_MAX) throw std::overflow_error("history identity exhausted");
    const auto sequence = Data.NextSequence++;
    if (Data.History.size() == HistoryCapacity) Data.History.erase(Data.History.begin());
    Data.History.push_back({sequence, fact});
    if (observed) Data.Discoveries.push_back({observed->ActorId, observed->Location, sequence});
}

Snapshot Journal::Capture(const api::CommitStamp &stamp) const {
    if ((!Data.WorldId.empty() && stamp.WorldId != Data.WorldId)
        || (Data.LastSource.WorldEpoch && stamp.WorldEpoch != Data.LastSource.WorldEpoch)
        || stamp.Revision < Data.LastRevision || stamp.Tick < Data.LastTick)
        throw std::logic_error("knowledge snapshot does not belong to this commit");
    Snapshot result;
    result.Stamp = stamp;
    result.HighWatermark = Data.NextSequence - 1;
    result.FirstSequence = Data.History.empty() ? Data.NextSequence : Data.History.front().Sequence;
    result.History = Data.History;
    result.Discoveries = Data.Discoveries;
    return result;
}

void Journal::Restore(const SaveData &data) {
    Validate(data);
    Data = data;
}

uint64_t Journal::RetainedBytes() const {
    return sizeof(*this) + Data.WorldId.capacity() + 1 + RecordBytes(Data.History)
        + Data.Discoveries.capacity() * sizeof(Discovery);
}
} // namespace pixel::knowledge
