#include "KnowledgeCodec.h"
#include "Runtime/Gameplay/Knowledge/Knowledge.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>

namespace pixel {
namespace {
constexpr size_t MaximumPayloadBytes = 1024 * 1024;

template<class T> void Read(std::istream &in, T &value) {
    in >> std::ws;
    if constexpr (std::is_unsigned_v<T>)
        if (in.peek() == '-') throw WorldStorageError("negative knowledge identity/count");
    if (!(in >> value)) throw WorldStorageError("truncated or invalid knowledge checkpoint");
    if constexpr (std::is_floating_point_v<T>)
        if (!std::isfinite(value)) throw WorldStorageError("nonfinite knowledge number");
}

template<class... T> void ReadAll(std::istream &in, T &...values) { (Read(in, values), ...); }

template<class T> void Write(std::ostream &out, const T &value) {
    if constexpr (std::is_floating_point_v<T>)
        if (!std::isfinite(value)) throw std::invalid_argument("nonfinite knowledge number");
    out << value << ' ';
}

template<class... T> void WriteAll(std::ostream &out, const T &...values) { (Write(out, values), ...); }

std::string ReadTag(std::istream &in) {
    std::string value;
    Read(in, value);
    if (value.size() > 32) throw WorldStorageError("oversized knowledge type tag");
    return value;
}

std::string ReadWorldId(std::istream &in) {
    in >> std::ws;
    std::string value;
    if (in.peek() != '"' || !(in >> std::quoted(value)) || value.size() > 128)
        throw WorldStorageError("invalid knowledge world identity");
    return value;
}

size_t ReadCount(std::istream &in, size_t maximum) {
    size_t value;
    Read(in, value);
    if (value > maximum) throw WorldStorageError("knowledge checkpoint capacity exceeded");
    return value;
}

template<class T> T ReadEnum(std::istream &in, T maximum) {
    uint32_t value;
    Read(in, value);
    if (value > uint32_t(maximum)) throw WorldStorageError("unknown knowledge enum value");
    return T(value);
}

template<class T> uint32_t EnumValue(T value, T maximum) {
    if (uint32_t(value) > uint32_t(maximum)) throw std::invalid_argument("unknown knowledge enum value");
    return uint32_t(value);
}

void WritePosition(std::ostream &out, const api::Position &value) {
    if (value.Offset[0] < 0 || value.Offset[0] >= 1 || value.Offset[1] < 0 || value.Offset[1] >= 1)
        throw std::invalid_argument("unnormalized knowledge position");
    WriteAll(out, value.AnchorCell.X, value.AnchorCell.Y, value.Offset[0], value.Offset[1]);
}

api::Position ReadPosition(std::istream &in) {
    api::Position value;
    ReadAll(in, value.AnchorCell.X, value.AnchorCell.Y, value.Offset[0], value.Offset[1]);
    if (value.Offset[0] < 0 || value.Offset[0] >= 1 || value.Offset[1] < 0 || value.Offset[1] >= 1)
        throw WorldStorageError("unnormalized knowledge position");
    return value;
}

void WriteRequest(std::ostream &out, const api::RequestKey &value) {
    WriteAll(out, value.WorldEpoch, value.Caller, value.Sequence);
}

api::RequestKey ReadRequest(std::istream &in) {
    api::RequestKey value;
    ReadAll(in, value.WorldEpoch, value.Caller, value.Sequence);
    return value;
}

void WriteCause(std::ostream &out, const api::RequestKey &value) {
    Write(out, "request");
    WriteRequest(out, value);
}

void WriteCause(std::ostream &out, const api::ProgramEffectCause &value) {
    WriteAll(out, "program_effect", value.Instance.WorldEpoch, value.Instance.InstanceId,
        value.Program.ResourceId, value.Program.Version, value.EmissionSequence, bool(value.ActivationRequest));
    if (value.ActivationRequest) WriteRequest(out, *value.ActivationRequest);
    Write(out, bool(value.ParentFact));
    if (value.ParentFact) WriteAll(out, value.ParentFact->WorldEpoch, value.ParentFact->Sequence);
}

void WriteCause(std::ostream &out, const api::DeviceCause &value) {
    WriteAll(out, "device", value.DeviceId, value.InstanceId);
}

void WriteCause(std::ostream &out, const api::AuthorityCause &value) {
    WriteAll(out, "authority", value.Version);
}

api::FactCause ReadCause(std::istream &in) {
    const auto tag = ReadTag(in);
    if (tag == "request") return ReadRequest(in);
    if (tag == "program_effect") {
        api::ProgramEffectCause value;
        bool activation, parent;
        ReadAll(in, value.Instance.WorldEpoch, value.Instance.InstanceId, value.Program.ResourceId,
            value.Program.Version, value.EmissionSequence, activation);
        if (activation) value.ActivationRequest = ReadRequest(in);
        Read(in, parent);
        if (parent) {
            api::FactId id;
            ReadAll(in, id.WorldEpoch, id.Sequence);
            value.ParentFact = id;
        }
        return value;
    }
    if (tag == "device") {
        api::DeviceCause value;
        ReadAll(in, value.DeviceId, value.InstanceId);
        return value;
    }
    if (tag == "authority") {
        api::AuthorityCause value;
        Read(in, value.Version);
        return value;
    }
    throw WorldStorageError("unknown knowledge fact cause");
}

// Stable names deliberately do not depend on the ordering of FactPayload.
void WritePayload(std::ostream &out, const api::MaterialRemoved &value) {
    WriteAll(out, "material_removed", value.Target.X, value.Target.Y, value.OldMaterial, value.NewMaterial);
}

void WritePayload(std::ostream &out, const api::MaterialPlaced &value) {
    WriteAll(out, "material_placed", value.Target.X, value.Target.Y, value.OldMaterial, value.NewMaterial);
}

void WritePayload(std::ostream &out, const api::ProgramActivated &value) {
    WriteAll(out, "program_activated", value.Program.ResourceId, value.Program.Version, value.ActivationId);
}

void WritePayload(std::ostream &out, const api::WindConfigured &value) {
    WriteAll(out, "wind_configured", EnumValue(value.Before.Kind, api::WindKind::Empty), value.Before.BoundaryY,
        EnumValue(value.After.Kind, api::WindKind::Empty), value.After.BoundaryY);
}

void WritePayload(std::ostream &out, const api::WindPulseSubmitted &value) {
    Write(out, "wind_pulse");
    WritePosition(out, value.Center);
    WriteAll(out, value.RadiusCells, value.RequestedDeltaVelocityCellsPerSecond[0],
        value.RequestedDeltaVelocityCellsPerSecond[1], value.Instance.WorldEpoch, value.Instance.InstanceId);
}

void WritePayload(std::ostream &out, const api::MaterialRemovedInDisk &value) {
    Write(out, "material_removed_disk");
    WritePosition(out, value.Center);
    WriteAll(out, value.RadiusCells, value.Changed, value.Blocked, value.NotReady);
}

void WritePayload(std::ostream &out, const api::DeviceChanged &value) {
    const auto &device = value.Device;
    WriteAll(out, "device_changed", device.Ref.Id, device.Ref.Version, device.Owner, device.X, device.Y,
        device.Enabled, EnumValue(device.State, DeviceState::WaitingForCapacity), device.Instance, device.Cycles,
        device.Fuel, EnumValue(value.Event, api::DeviceEvent::StateChanged));
}

void WritePayload(std::ostream &out, const api::FieldSourceChanged &value) {
    WriteAll(out, "field_source", value.Handle, EnumValue(value.Event, api::FieldSourceEvent::Stopped));
}

void WritePayload(std::ostream &out, const api::ForceObserved &value) {
    WriteAll(out, "force_observed", value.ActorId, value.Location.X, value.Location.Y,
        value.AccelerationCellsPerSecondSquared[0], value.AccelerationCellsPerSecondSquared[1]);
}

void WritePayload(std::ostream &out, const api::AuthorityChanged &value) {
    WriteAll(out, "authority_changed", value.Version, value.DeviceId, value.Region.Origin.X,
        value.Region.Origin.Y, value.Region.Width, value.Region.Height, value.Protected,
        value.ObservationAllowed, value.InterventionAllowed);
}

void WritePayload(std::ostream &out, const api::InterventionStopped &value) {
    WriteAll(out, "intervention_stopped", value.Instance.WorldEpoch, value.Instance.InstanceId,
        value.Subject, EnumValue(value.Reason, api::InterventionStopReason::LeaseExpired));
}

api::FactPayload ReadPayload(std::istream &in) {
    const auto tag = ReadTag(in);
    if (tag == "material_removed") {
        api::MaterialRemoved value;
        ReadAll(in, value.Target.X, value.Target.Y, value.OldMaterial, value.NewMaterial);
        return value;
    }
    if (tag == "material_placed") {
        api::MaterialPlaced value;
        ReadAll(in, value.Target.X, value.Target.Y, value.OldMaterial, value.NewMaterial);
        return value;
    }
    if (tag == "program_activated") {
        api::ProgramActivated value;
        ReadAll(in, value.Program.ResourceId, value.Program.Version, value.ActivationId);
        return value;
    }
    if (tag == "wind_configured") {
        api::WindConfigured value;
        value.Before.Kind = ReadEnum(in, api::WindKind::Empty);
        Read(in, value.Before.BoundaryY);
        value.After.Kind = ReadEnum(in, api::WindKind::Empty);
        Read(in, value.After.BoundaryY);
        return value;
    }
    if (tag == "wind_pulse") {
        api::WindPulseSubmitted value;
        value.Center = ReadPosition(in);
        ReadAll(in, value.RadiusCells, value.RequestedDeltaVelocityCellsPerSecond[0],
            value.RequestedDeltaVelocityCellsPerSecond[1], value.Instance.WorldEpoch, value.Instance.InstanceId);
        return value;
    }
    if (tag == "material_removed_disk") {
        api::MaterialRemovedInDisk value;
        value.Center = ReadPosition(in);
        ReadAll(in, value.RadiusCells, value.Changed, value.Blocked, value.NotReady);
        return value;
    }
    if (tag == "device_changed") {
        api::DeviceChanged value;
        auto &device = value.Device;
        ReadAll(in, device.Ref.Id, device.Ref.Version, device.Owner, device.X, device.Y, device.Enabled);
        device.State = ReadEnum(in, DeviceState::WaitingForCapacity);
        ReadAll(in, device.Instance, device.Cycles, device.Fuel);
        value.Event = ReadEnum(in, api::DeviceEvent::StateChanged);
        return value;
    }
    if (tag == "field_source") {
        api::FieldSourceChanged value;
        Read(in, value.Handle);
        value.Event = ReadEnum(in, api::FieldSourceEvent::Stopped);
        return value;
    }
    if (tag == "force_observed") {
        api::ForceObserved value;
        ReadAll(in, value.ActorId, value.Location.X, value.Location.Y,
            value.AccelerationCellsPerSecondSquared[0], value.AccelerationCellsPerSecondSquared[1]);
        return value;
    }
    if (tag == "authority_changed") {
        api::AuthorityChanged value;
        ReadAll(in, value.Version, value.DeviceId, value.Region.Origin.X, value.Region.Origin.Y,
            value.Region.Width, value.Region.Height, value.Protected, value.ObservationAllowed,
            value.InterventionAllowed);
        return value;
    }
    if (tag == "intervention_stopped") {
        api::InterventionStopped value;
        ReadAll(in, value.Instance.WorldEpoch, value.Instance.InstanceId, value.Subject);
        value.Reason = ReadEnum(in, api::InterventionStopReason::LeaseExpired);
        return value;
    }
    throw WorldStorageError("unknown knowledge fact payload");
}

void WriteRecord(std::ostream &out, const knowledge::HistoryRecord &record) {
    const auto &fact = record.Source;
    const auto &stamp = fact.CommittedAt;
    WriteAll(out, record.Sequence, fact.Id.WorldEpoch, fact.Id.Sequence, std::quoted(stamp.WorldId),
        stamp.WorldEpoch, stamp.Tick, stamp.Revision);
    std::visit([&](const auto &cause) { WriteCause(out, cause); }, fact.Cause);
    std::visit([&](const auto &payload) { WritePayload(out, payload); }, fact.Payload);
    out << '\n';
}

knowledge::HistoryRecord ReadRecord(std::istream &in) {
    knowledge::HistoryRecord record;
    auto &fact = record.Source;
    auto &stamp = fact.CommittedAt;
    ReadAll(in, record.Sequence, fact.Id.WorldEpoch, fact.Id.Sequence);
    stamp.WorldId = ReadWorldId(in);
    ReadAll(in, stamp.WorldEpoch, stamp.Tick, stamp.Revision);
    fact.Cause = ReadCause(in);
    fact.Payload = ReadPayload(in);
    return record;
}
} // namespace

std::string EncodeKnowledgeState(const knowledge::SaveData &state) {
    knowledge::Journal validator;
    validator.Restore(state);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << "PixelKnowledge 1\n";
    WriteAll(out, state.Version, std::quoted(state.WorldId), state.NextSequence, state.RuntimeFirstSequence,
        state.LastSource.WorldEpoch, state.LastSource.Sequence, state.LastRevision, state.LastTick);
    out << '\n';
    WriteAll(out, state.History.size(), state.Discoveries.size());
    out << '\n';
    for (const auto &record : state.History) WriteRecord(out, record);
    for (const auto &entry : state.Discoveries) {
        WriteAll(out, entry.OwnerActor, entry.Location.X, entry.Location.Y, entry.EvidenceSequence,
            entry.Observed, entry.Analyzed, entry.CanCopy, entry.CanEdit);
        out << '\n';
    }
    return out.str();
}

knowledge::SaveData DecodeKnowledgeState(std::string_view payload) {
    if (payload.size() > MaximumPayloadBytes) throw WorldStorageError("oversized knowledge checkpoint");
    std::istringstream in{std::string(payload)};
    in.imbue(std::locale::classic());
    const auto magic = ReadTag(in);
    uint32_t version;
    Read(in, version);
    if (magic != "PixelKnowledge" || version != 1) throw WorldStorageError("unsupported knowledge checkpoint");

    knowledge::SaveData state;
    Read(in, state.Version);
    state.WorldId = ReadWorldId(in);
    ReadAll(in, state.NextSequence, state.RuntimeFirstSequence, state.LastSource.WorldEpoch,
        state.LastSource.Sequence, state.LastRevision, state.LastTick);
    const auto historyCount = ReadCount(in, knowledge::HistoryCapacity);
    const auto discoveryCount = ReadCount(in, knowledge::DiscoveryCapacity);
    state.History.reserve(historyCount);
    for (size_t index = 0; index < historyCount; ++index) state.History.push_back(ReadRecord(in));
    state.Discoveries.resize(discoveryCount);
    for (auto &entry : state.Discoveries)
        ReadAll(in, entry.OwnerActor, entry.Location.X, entry.Location.Y, entry.EvidenceSequence,
            entry.Observed, entry.Analyzed, entry.CanCopy, entry.CanEdit);
    in >> std::ws;
    if (!in.eof()) throw WorldStorageError("unexpected knowledge checkpoint data");

    // Reuse the domain's sequence, source watermark and discovery evidence rules.
    // Only invalid external DTOs change error type; allocation and logic faults escape.
    knowledge::Journal validator;
    try { validator.Restore(state); }
    catch (const std::invalid_argument &error) { throw WorldStorageError(error.what()); }
    return state;
}
} // namespace pixel
