#pragma once

#include "ProcessCarrier.h"
#include "Runtime/System/Serialization/TextArchive.h"
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <type_traits>

namespace pixel::checkpoint {
namespace process = formula::process;
constexpr size_t MaximumBytes = 2 * 1024 * 1024;
inline void Require(bool condition, const char *message) {
    if (!condition) throw std::invalid_argument(message);
}

using Writer = serialization::Writer<MaximumBytes>;
using Reader = serialization::Reader<MaximumBytes>;

inline void Write(Writer &out, const process::Position &value) {
    out.Numbers(value.CellX, value.CellY, value.OffsetX, value.OffsetY);
}
inline process::Position ReadPosition(Reader &in) {
    process::Position value;
    in.Numbers(value.CellX, value.CellY, value.OffsetX, value.OffsetY);
    Require(value.OffsetX >= 0 && value.OffsetX < 1 && value.OffsetY >= 0 && value.OffsetY < 1,
            "invalid checkpoint position offset");
    const int64_t maximum = (int64_t{1} << 49) * storage::ChunkEdge;
    const auto lastCell = maximum + storage::ChunkEdge - 1;
    Require(value.CellX >= -maximum && value.CellX <= lastCell && value.CellY >= -maximum && value.CellY <= lastCell,
            "checkpoint position outside world range");
    return value;
}
inline void Write(Writer &out, WorldPosition value) {
    value.Normalize();
    const auto cell = value.Cell();
    const auto offset = value.RelativeTo(cell);
    Write(out, process::Position{cell.X, cell.Y, offset[0], offset[1]});
}
inline WorldPosition ReadWorldPosition(Reader &in) {
    const auto value = ReadPosition(in);
    const auto chunk = ChunkOf({value.CellX, value.CellY});
    return {chunk, {double(value.CellX - chunk.X * storage::ChunkEdge) + value.OffsetX,
                    double(value.CellY - chunk.Y * storage::ChunkEdge) + value.OffsetY}};
}
inline void Write(Writer &out, const process::ProgramIdentity &value) {
    out.String(value.Resource); out.String(value.ContentSha256);
    out.Numbers(value.SemanticsVersion, value.HostContractVersion);
}
inline process::ProgramIdentity ReadIdentity(Reader &in) {
    process::ProgramIdentity value;
    value.Resource = in.String(256); value.ContentSha256 = in.String(64);
    in.Numbers(value.SemanticsVersion, value.HostContractVersion);
    return value;
}
inline void Write(Writer &out, const process::ProgramArtifact &value) {
    const auto record = value.Export();
    out.Numbers(record.SchemaVersion); Write(out, record.Identity); out.String(record.Source);
}
inline process::ProgramArtifact ReadProgram(Reader &in) {
    process::ProgramRecord record;
    record.SchemaVersion = in.Number<uint32_t>(); record.Identity = ReadIdentity(in); record.Source = in.String(16384);
    auto result = process::ImportProgram(record);
    if (const auto *error = std::get_if<process::Diagnostic>(&result)) throw std::invalid_argument(error->Message);
    return std::get<process::ProgramArtifact>(std::move(result));
}
inline void Write(Writer &out, const process::HostEvent &event) {
    out.Numbers(event.Sequence, event.CommittedAt, event.Target); out.Enum(event.Result);
    out.Numbers(event.Request.Instance, event.Request.Sequence, event.WakeSubscription.Instance, event.WakeSubscription.Sequence);
    Write(out, event.HitPosition); out.Numbers(event.Handle.Id);
}
inline process::HostEvent ReadEvent(Reader &in) {
    process::HostEvent event;
    in.Numbers(event.Sequence, event.CommittedAt, event.Target); event.Result = in.Enum(process::Outcome::Timeout);
    in.Numbers(event.Request.Instance, event.Request.Sequence, event.WakeSubscription.Instance, event.WakeSubscription.Sequence);
    event.HitPosition = ReadPosition(in); event.Handle.Id = in.Number<uint64_t>();
    return event;
}
inline void Write(Writer &out, const std::optional<process::HostEvent> &event) {
    out.Numbers(bool(event)); if (event) Write(out, *event);
}
inline std::optional<process::HostEvent> ReadOptionalEvent(Reader &in) {
    if (!in.Boolean()) return std::nullopt;
    return ReadEvent(in);
}
inline void Write(Writer &out, const process::FieldSource &value) {
    out.Numbers(value.Radius, value.AccelerationX, value.AccelerationY, value.Lifetime);
}
inline process::FieldSource ReadField(Reader &in) {
    process::FieldSource value; in.Numbers(value.Radius, value.AccelerationX, value.AccelerationY, value.Lifetime); return value;
}
inline void Write(Writer &out, const process::Effect &value) {
    out.Numbers(value.index());
    std::visit([&](const auto &effect) {
        using T = std::decay_t<decltype(effect)>;
        if constexpr (std::is_same_v<T, process::Dig>) out.Numbers(effect.Radius);
        else if constexpr (std::is_same_v<T, process::Wind>) out.Numbers(effect.Radius, effect.VelocityX, effect.VelocityY);
        else if constexpr (std::is_same_v<T, process::Bolt>) out.Numbers(effect.Damage, effect.VelocityX, effect.Lifetime);
        else if constexpr (std::is_same_v<T, process::FieldSource>) Write(out, effect);
        else if constexpr (std::is_same_v<T, process::UpdateField>) { out.Numbers(effect.Handle.Id); Write(out, effect.Source); }
        else out.Numbers(effect.Handle.Id);
    }, value);
}
inline process::Effect ReadEffect(Reader &in) {
    switch (in.Number<unsigned>()) {
    case 0: return process::Dig{in.Number<double>()};
    case 1: { process::Wind value; in.Numbers(value.Radius, value.VelocityX, value.VelocityY); return value; }
    case 2: { process::Bolt value; in.Numbers(value.Damage, value.VelocityX, value.Lifetime); return value; }
    case 3: return ReadField(in);
    case 4: { process::UpdateField value; value.Handle.Id = in.Number<uint64_t>(); value.Source = ReadField(in); return value; }
    case 5: return process::StopField{{in.Number<uint64_t>()}};
    default: throw std::invalid_argument("invalid checkpoint effect type");
    }
}
inline void Write(Writer &out, const process::Value &value) {
    out.Numbers(value.index());
    std::visit([&](const auto &entry) {
        using T = std::decay_t<decltype(entry)>;
        if constexpr (std::is_arithmetic_v<T>) out.Numbers(entry);
        else if constexpr (std::is_same_v<T, process::Effect>) Write(out, entry);
        else if constexpr (std::is_same_v<T, process::Outcome>) out.Enum(entry);
        else if constexpr (std::is_same_v<T, process::EffectHandle>) out.Numbers(entry.Id);
        else out.Numbers(entry.Value);
    }, value);
}
inline process::Value ReadValue(Reader &in) {
    switch (in.Number<unsigned>()) {
    case 0: return in.Boolean();
    case 1: return in.Number<int64_t>();
    case 2: return in.Number<double>();
    case 3: return process::Cells{in.Number<double>()};
    case 4: return process::Speed{in.Number<double>()};
    case 5: return process::Duration{in.Number<uint64_t>()};
    case 6: return ReadEffect(in);
    case 7: return in.Enum(process::Outcome::Timeout);
    case 8: return process::Acceleration{in.Number<double>()};
    case 9: return process::EffectHandle{in.Number<uint64_t>()};
    default: throw std::invalid_argument("invalid checkpoint local type");
    }
}
inline void Write(Writer &out, const process::InstanceSnapshot &value) {
    out.Numbers(value.SchemaVersion); Write(out, value.Program);
    const auto &state = value.State;
    out.Numbers(state.Id); out.Enum(state.Status);
    out.Numbers(state.Instruction, state.ActiveSteps, state.Remaining, state.LastRequest.Instance, state.LastRequest.Sequence);
    out.Enum(state.LastOutcome); Write(out, state.Origin);
    out.Numbers(bool(state.WakeSubscription));
    if (state.WakeSubscription) out.Numbers(state.WakeSubscription->Instance, state.WakeSubscription->Sequence);
    out.Numbers(state.LastEffectHandle.Id, value.Limits.Work, value.Limits.Queries, value.Limits.Outputs);
    for (const auto &local : value.Locals) { out.Numbers(bool(local)); if (local) Write(out, *local); }
    out.Numbers(value.LastBoundary, value.EventSequence, value.SubscriptionSequence); out.Enum(value.WaitingFor);
    Write(out, value.Receipt); Write(out, value.Impact); Write(out, value.Wake);
    out.Numbers(value.ReceiptSeen, value.ImpactSeen);
    if (value.SchemaVersion == 3) {
        out.Numbers(value.OriginPinned, value.OriginScopes.size());
        for (const auto &scope : value.OriginScopes) { Write(out, scope.Origin); out.Numbers(scope.Pinned); }
    }
}
inline process::InstanceSnapshot ReadInstance(Reader &in) {
    process::InstanceSnapshot value;
    value.SchemaVersion = in.Number<uint32_t>(); value.Program = ReadIdentity(in);
    auto &state = value.State;
    state.Id = in.Number<uint64_t>(); state.Status = in.Enum(process::Phase::Ended);
    in.Numbers(state.Instruction, state.ActiveSteps, state.Remaining, state.LastRequest.Instance, state.LastRequest.Sequence);
    state.LastOutcome = in.Enum(process::Outcome::Timeout); state.Origin = ReadPosition(in);
    if (in.Boolean()) { process::Subscription subscription; in.Numbers(subscription.Instance, subscription.Sequence); state.WakeSubscription = subscription; }
    in.Numbers(state.LastEffectHandle.Id, value.Limits.Work, value.Limits.Queries, value.Limits.Outputs);
    for (auto &local : value.Locals) if (in.Boolean()) local = ReadValue(in);
    in.Numbers(value.LastBoundary, value.EventSequence, value.SubscriptionSequence); value.WaitingFor = in.Enum(process::AwaitKind::Wake);
    value.Receipt = ReadOptionalEvent(in); value.Impact = ReadOptionalEvent(in); value.Wake = ReadOptionalEvent(in);
    value.ReceiptSeen = in.Boolean(); value.ImpactSeen = in.Boolean();
    if (value.SchemaVersion == 3) {
        value.OriginPinned = in.Boolean();
        const auto count = in.Count(24);
        for (size_t i = 0; i < count; ++i) value.OriginScopes.push_back({ReadPosition(in), in.Boolean()});
    }
    return value;
}
inline void Write(Writer &out, const ProcessCarrier &value) {
    out.Enum(value.Kind); out.Numbers(value.Subject); Write(out, value.Origin);
    out.Numbers(value.Scope.X, value.Scope.Y, value.Scope.Width, value.Scope.Height,
        value.AngleDegrees, value.MaximumRadius, value.ActivityRadius, value.Credits, value.Grants,
        value.Limits.Work, value.Limits.Queries, value.Limits.Outputs);
    out.Enum(value.Binding); out.Numbers(value.AttachedActor.Value, value.LocalOffset[0], value.LocalOffset[1]);
}
inline ProcessCarrier ReadCarrier(Reader &in, uint32_t hostVersion) {
    ProcessCarrier value;
    value.Kind = in.Enum(ProcessCarrierKind::Intervention); value.Subject = in.Number<uint64_t>(); value.Origin = ReadWorldPosition(in);
    in.Numbers(value.Scope.X, value.Scope.Y, value.Scope.Width, value.Scope.Height,
        value.AngleDegrees, value.MaximumRadius, value.ActivityRadius, value.Credits, value.Grants,
        value.Limits.Work, value.Limits.Queries, value.Limits.Outputs);
    if (hostVersion >= 2) {
        value.Binding = in.Enum(ProcessBindingKind::Actor);
        in.Numbers(value.AttachedActor.Value, value.LocalOffset[0], value.LocalOffset[1]);
        Require((value.Binding == ProcessBindingKind::Actor) == (value.AttachedActor.Value != 0)
            && std::abs(value.LocalOffset[0]) <= 2048 && std::abs(value.LocalOffset[1]) <= 2048
            && (value.Binding == ProcessBindingKind::Actor || value.LocalOffset == std::array<double, 2>{}),
            "invalid checkpoint actor binding");
    }
    const int64_t maximum = (int64_t{1} << 49) * storage::ChunkEdge;
    const auto lastCell = maximum + storage::ChunkEdge - 1;
    Require(value.Subject != 0 && value.Credits >= 0 && value.MaximumRadius >= 0 && value.MaximumRadius <= 16
        && value.ActivityRadius >= 0 && value.ActivityRadius <= 16 && (value.Grants & ~process::AllCapabilities) == 0
        && value.Limits.Work > 0 && value.Limits.Work <= 256 && value.Limits.Queries <= 8 && value.Limits.Outputs <= 16
        && !value.Scope.Empty() && value.Scope.Width <= 2048 && value.Scope.Height <= 2048
        && value.Scope.X >= -maximum && value.Scope.X <= lastCell - value.Scope.Width
        && value.Scope.Y >= -maximum && value.Scope.Y <= lastCell - value.Scope.Height,
        "invalid checkpoint process carrier");
    Require(value.Scope.Contains(value.Origin.Cell()), "checkpoint carrier origin outside scope");
    return value;
}

} // namespace pixel::checkpoint
