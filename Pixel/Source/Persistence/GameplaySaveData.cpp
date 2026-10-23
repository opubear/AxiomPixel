#include "GameplaySaveData.h"
#include "World/WorldStorageError.h"
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>

namespace pixel {
namespace {
template<class T> void Read(std::istream &in, T &value) {
    in >> std::ws;
    if constexpr (std::is_unsigned_v<T>)
        if (in.peek() == '-') throw WorldStorageError("negative checkpoint identity/count");
    if (!(in >> value)) throw WorldStorageError("truncated or invalid gameplay checkpoint");
    if constexpr (std::is_floating_point_v<T>)
        if (!std::isfinite(value)) throw WorldStorageError("nonfinite checkpoint number");
}
template<class... T> void ReadAll(std::istream &in, T &...values) { (Read(in, values), ...); }
size_t Count(std::istream &in, size_t maximum) {
    size_t size = 0;
    Read(in, size);
    if (size > maximum) throw WorldStorageError("gameplay checkpoint capacity exceeded");
    return size;
}
void WritePosition(std::ostream &out, WorldPosition p) {
    out << p.Chunk.X << ' ' << p.Chunk.Y << ' ' << p.Local[0] << ' ' << p.Local[1] << ' ';
}
WorldPosition ReadPosition(std::istream &in) {
    WorldPosition p;
    ReadAll(in, p.Chunk.X, p.Chunk.Y, p.Local[0], p.Local[1]);
    p.Normalize();
    return p;
}
}

std::string EncodeGameplayState(const GameplaySaveData &state) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "PixelGameplay 2\n" << state.Tick << ' ' << state.Revision << ' ' << state.PlayerBag.Value
        << ' ' << state.LastAim[0] << ' ' << state.LastAim[1] << '\n';
    out << state.Actors.NextId << ' ' << state.Actors.Records.size() << '\n';
    for (const auto &actor : state.Actors.Records) {
        const auto body = actor.Body.CaptureState();
        out << actor.Id.Value << ' ' << uint32_t(actor.Kind) << ' ';
        WritePosition(out, body.Position);
        out << body.VerticalSpeed << ' ' << body.Grounded << '\n';
    }
    out << state.Inventory.NextContainer << ' ' << state.Inventory.NextReservation << ' '
        << state.Inventory.Containers.size() << '\n';
    for (const auto &container : state.Inventory.Containers) {
        out << container.Ref.Id.Value << ' ' << container.Ref.Version << ' ' << container.Capacity << ' '
            << container.Quantity << ' ' << container.Stacks.size() << '\n';
        for (const auto &stack : container.Stacks) {
            if (stack.Reserved) throw std::logic_error("checkpoint contains pending item reservation");
            out << uint32_t(stack.Item) << ' ' << items::Definition(stack.Item).Version << ' ' << stack.Quantity << '\n';
        }
    }
    out << state.Devices.NextId << ' ' << state.Devices.Records.size() << '\n';
    for (const auto &record : state.Devices.Records) {
        const auto &d = record.Value;
        out << ForceEmitterType << " 1 " << d.Ref.Id << ' ' << d.Ref.Version << ' ' << d.Owner << ' '
            << d.X << ' ' << d.Y << ' ' << d.Enabled << ' ' << uint32_t(d.State) << ' ' << d.Instance << ' '
            << d.Cycles << ' ' << d.Fuel << ' ' << record.FuelContainer.Value << '\n';
    }
    out << state.FieldSources.NextId << ' ' << state.FieldSources.Sources.size() << '\n';
    for (const auto &s : state.FieldSources.Sources) {
        out << s.Handle.Id << ' ' << s.Owner << ' ';
        WritePosition(out, s.Spec.Center);
        out << s.Spec.Radius << ' ' << s.Spec.Acceleration.X << ' ' << s.Spec.Acceleration.Y << ' '
            << s.Spec.Lifetime << ' ' << s.Remaining << ' ' << s.Suspended << '\n';
    }
    out << std::quoted(state.Programs) << '\n';
    out << state.Garden.Enabled << ' ' << state.Garden.CrystalEnabled << ' ' << state.Garden.SurfaceY << ' '
        << state.Garden.WildGrowth << ' ' << state.Garden.HomeGrowth << '\n';
    return out.str();
}

GameplaySaveData DecodeGameplayState(std::string_view payload) {
    if (payload.size() > 8 * 1024 * 1024) throw WorldStorageError("oversized gameplay checkpoint");
    std::istringstream in{std::string(payload)};
    std::string magic;
    unsigned version = 0;
    ReadAll(in, magic, version);
    if (magic != "PixelGameplay" || (version != 1 && version != 2)) throw WorldStorageError("unsupported gameplay checkpoint");
    GameplaySaveData state;
    ReadAll(in, state.Tick, state.Revision, state.PlayerBag.Value, state.LastAim[0], state.LastAim[1]);
    if (!state.Revision || state.Revision == UINT64_MAX || state.Tick == UINT64_MAX
        || !state.PlayerBag.Value || std::abs(std::hypot(state.LastAim[0], state.LastAim[1]) - 1) > 1e-9)
        throw WorldStorageError("invalid saved commit or aiming state");
    Read(in, state.Actors.NextId);
    state.Actors.Records.resize(Count(in, Actors::MaximumCount));
    for (auto &actor : state.Actors.Records) {
        unsigned kind;
        ReadAll(in, actor.Id.Value, kind);
        actor.Kind = ActorKind(kind);
        CharacterState body;
        body.Position = ReadPosition(in);
        ReadAll(in, body.VerticalSpeed, body.Grounded);
        actor.Body.RestoreState(body);
    }
    ReadAll(in, state.Inventory.NextContainer, state.Inventory.NextReservation);
    state.Inventory.Containers.resize(Count(in, items::Inventory::MaximumContainers));
    for (auto &container : state.Inventory.Containers) {
        ReadAll(in, container.Ref.Id.Value, container.Ref.Version, container.Capacity, container.Quantity);
        container.Stacks.resize(Count(in, items::Definitions().size()));
        for (auto &stack : container.Stacks) {
            uint32_t item, contentVersion;
            ReadAll(in, item, contentVersion, stack.Quantity);
            stack.Item = items::ItemId(item);
            if (!items::IsKnown(stack.Item) || items::Definition(stack.Item).Version != contentVersion)
                throw WorldStorageError("missing item content version");
        }
    }
    Read(in, state.Devices.NextId);
    state.Devices.Records.resize(Count(in, Devices::MaximumCount));
    for (auto &record : state.Devices.Records) {
        auto &d = record.Value;
        uint32_t type, contentVersion, phase;
        ReadAll(in, type, contentVersion, d.Ref.Id, d.Ref.Version, d.Owner, d.X, d.Y, d.Enabled,
                phase, d.Instance, d.Cycles, d.Fuel, record.FuelContainer.Value);
        if (type != ForceEmitterType || contentVersion != 1) throw WorldStorageError("missing device content version");
        d.State = DeviceState(phase);
    }
    Read(in, state.FieldSources.NextId);
    state.FieldSources.Sources.resize(Count(in, FieldSources::Capacity));
    for (auto &s : state.FieldSources.Sources) {
        ReadAll(in, s.Handle.Id, s.Owner);
        s.Spec.Center = ReadPosition(in);
        ReadAll(in, s.Spec.Radius, s.Spec.Acceleration.X, s.Spec.Acceleration.Y, s.Spec.Lifetime, s.Remaining, s.Suspended);
    }
    if (!(in >> std::quoted(state.Programs)) || state.Programs.empty()) throw WorldStorageError("missing program checkpoint");
    if (version >= 2) {
        auto &garden = state.Garden;
        ReadAll(in, garden.Enabled, garden.CrystalEnabled, garden.SurfaceY, garden.WildGrowth, garden.HomeGrowth);
        if (garden.SurfaceY < INT64_MIN + 32 || garden.SurfaceY > INT64_MAX - 32
            || garden.WildGrowth > GardenMatureSteps || garden.HomeGrowth > GardenMatureSteps
            || (!garden.Enabled && (garden.WildGrowth || garden.HomeGrowth)))
            throw WorldStorageError("invalid force garden checkpoint");
    }
    in >> std::ws;
    if (!in.eof()) throw WorldStorageError("unexpected gameplay checkpoint data");
    return state;
}
} // namespace pixel
