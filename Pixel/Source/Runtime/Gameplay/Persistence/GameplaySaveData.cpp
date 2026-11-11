#include "GameplaySaveData.h"
#include "Runtime/Simulation/World/WorldStorageError.h"
#include "KnowledgeCodec.h"
#include "WorldWillCodec.h"
#include "Runtime/Gameplay/FormulaBridge/CheckpointCodec.h"
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
    // Versions 1-3 store cells. Keep the wire format stable across the cm migration.
    out << p.Chunk.X << ' ' << p.Chunk.Y << ' ' << double(p.Local[0]) / CentimetersPerCell
        << ' ' << double(p.Local[1]) / CentimetersPerCell << ' ';
}
WorldPosition ReadPosition(std::istream &in) {
    CellCoord chunk;
    std::array<double, 2> cells;
    ReadAll(in, chunk.X, chunk.Y, cells[0], cells[1]);
    return WorldPosition::FromChunkCells(chunk, cells);
}
void WriteActor(std::ostream &out, const ActorRecord &actor) {
    const auto body = actor.Body.CaptureState();
    out << actor.Id.Value << ' ' << uint32_t(actor.Kind) << ' ';
    WritePosition(out, body.Position);
    out << double(body.VerticalSpeed) / CentimetersPerCell << ' ' << body.Grounded << '\n';
    out << actor.Monster.has_value() << ' ';
    if (actor.Monster) {
        const auto &monster = *actor.Monster;
        out << std::quoted(monster.Definition) << ' ';
        WritePosition(out, monster.Home);
        out << uint32_t(monster.Action) << ' ' << monster.ActionTicks << ' ' << monster.CooldownTicks << ' ' << monster.FacingLeft << ' ';
    }
    out << actor.Health.has_value() << ' ';
    if (actor.Health) {
        const auto &health = *actor.Health;
        out << health.Current << ' ' << health.Maximum << ' ' << health.InvulnerableTicks << ' ';
        WritePosition(out, health.Respawn);
    }
    out << '\n';
}
ActorRecord ReadActor(std::istream &in, unsigned version) {
    ActorRecord actor;
    unsigned kind;
    ReadAll(in, actor.Id.Value, kind);
    if (kind > uint32_t(version < 4 ? ActorKind::TrainingDummy : ActorKind::Monster))
        throw WorldStorageError("unknown saved actor kind");
    actor.Kind = ActorKind(kind);
    BodyState body;
    body.Position = ReadPosition(in);
    double speedCells;
    ReadAll(in, speedCells, body.Grounded);
    body.VerticalSpeed = float(speedCells * CentimetersPerCell);
    actor.Body.RestoreState(body);
    if (version >= 4) {
        bool hasMonster = false, hasHealth = false;
        Read(in, hasMonster);
        if (hasMonster) {
            MonsterState monster;
            if (!(in >> std::quoted(monster.Definition)) || monster.Definition.size() > 32)
                throw WorldStorageError("invalid saved monster definition");
            monster.Home = ReadPosition(in);
            unsigned action;
            ReadAll(in, action, monster.ActionTicks, monster.CooldownTicks, monster.FacingLeft);
            if (action > uint32_t(MonsterAction::Death) || monster.ActionTicks > 3600 || monster.CooldownTicks > 3600)
                throw WorldStorageError("invalid saved monster behavior");
            monster.Action = MonsterAction(action);
            actor.Monster = std::move(monster);
        }
        Read(in, hasHealth);
        if (hasHealth) {
            HealthState health;
            ReadAll(in, health.Current, health.Maximum, health.InvulnerableTicks);
            health.Respawn = ReadPosition(in);
            actor.Health = health;
        }
    }
    return actor;
}
}

std::string EncodeGameplayState(const GameplaySaveData &state) {
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "PixelGameplay 6\n" << state.Tick << ' ' << state.Revision << ' ' << state.PlayerBag.Value
        << ' ' << state.LastAim[0] << ' ' << state.LastAim[1] << '\n';
    out << state.Actors.NextId << ' ' << state.Actors.Records.size() << '\n';
    for (const auto &actor : state.Actors.Records) WriteActor(out, actor);
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
    const auto &authority = state.Authority;
    out << authority.Version << ' ' << authority.Grants.Observe << ' ' << authority.Grants.Intervene << ' '
        << authority.Influence.Version << ' ' << authority.Influence.Available << ' ' << authority.Protections.size() << '\n';
    for (const auto &protection : authority.Protections) {
        const auto region = protection.Region;
        out << protection.DeviceId << ' ' << protection.Active << ' ' << region.Origin.X << ' ' << region.Origin.Y
            << ' ' << region.Width << ' ' << region.Height << '\n';
    }
    out << authority.NextIntervention << ' ' << authority.Interventions.size() << '\n';
    for (const auto &intervention : authority.Interventions) {
        const auto region = intervention.Scope;
        out << intervention.Id << ' ' << intervention.ExpiresAtTick << ' ' << region.Origin.X << ' ' << region.Origin.Y
            << ' ' << region.Width << ' ' << region.Height << '\n';
    }
    out << std::quoted(EncodeKnowledgeState(state.Knowledge)) << '\n'
        << std::quoted(EncodeWorldWillState(state.WorldWill)) << '\n';
    const auto &population = state.Population;
    if (population.Version > 1 || (population.Version == 0 && (!population.Decisions.empty() || !population.Dormant.empty())))
        throw WorldStorageError("invalid regional population version");
    out << population.Version << ' ' << population.Decisions.size() << ' ' << population.Dormant.size() << '\n';
    for (const auto &entry : population.Decisions)
        out << entry.Site.AnchorX << ' ' << uint32_t(entry.Site.Kind) << ' ' << std::quoted(entry.Site.Species)
            << ' ' << entry.Actor.Value << '\n';
    for (const auto &actor : population.Dormant) WriteActor(out, actor);
    out << state.Inventory.NextInstance << ' ' << state.GroundBag.Value << ' ' << state.DroppedItems.size() << '\n';
    for (const auto &drop : state.DroppedItems) {
        out << uint32_t(drop.Item.Item) << ' ' << drop.Item.Instance << ' ';
        WritePosition(out, drop.Position);
        out << '\n';
    }
    for (const auto &container : state.Inventory.Containers) {
        out << container.Instances.size() << '\n';
        for (const auto &item : container.Instances) {
            if (!item.Formula) throw std::logic_error("tool is missing its formula");
            checkpoint::Writer encoded;
            checkpoint::Write(encoded, *item.Formula);
            out << uint32_t(item.Ref.Item) << ' ' << item.Ref.Instance << ' ' << item.FormulaVersion << ' '
                << std::quoted(encoded.Finish()) << '\n';
        }
    }
    auto payload = out.str();
    if (payload.size() > 8 * 1024 * 1024) throw WorldStorageError("oversized gameplay checkpoint");
    return payload;
}

GameplaySaveData DecodeGameplayState(std::string_view payload) {
    if (payload.size() > 8 * 1024 * 1024) throw WorldStorageError("oversized gameplay checkpoint");
    std::istringstream in{std::string(payload)};
    std::string magic;
    unsigned version = 0;
    ReadAll(in, magic, version);
    if (magic != "PixelGameplay" || version < 1 || version > 6) throw WorldStorageError("unsupported gameplay checkpoint");
    GameplaySaveData state;
    ReadAll(in, state.Tick, state.Revision, state.PlayerBag.Value, state.LastAim[0], state.LastAim[1]);
    if (!state.Revision || state.Revision == UINT64_MAX || state.Tick == UINT64_MAX
        || !state.PlayerBag.Value || std::abs(std::hypot(state.LastAim[0], state.LastAim[1]) - 1) > 1e-9)
        throw WorldStorageError("invalid saved commit or aiming state");
    Read(in, state.Actors.NextId);
    state.Actors.Records.resize(Count(in, Actors::MaximumCount));
    for (auto &actor : state.Actors.Records) actor = ReadActor(in, version);
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
    if (version >= 3) {
        auto &authority = state.Authority;
        ReadAll(in, authority.Version, authority.Grants.Observe, authority.Grants.Intervene,
                authority.Influence.Version, authority.Influence.Available);
        authority.Protections.resize(Count(in, Devices::MaximumCount));
        for (auto &protection : authority.Protections)
            ReadAll(in, protection.DeviceId, protection.Active, protection.Region.Origin.X, protection.Region.Origin.Y,
                    protection.Region.Width, protection.Region.Height);
        Read(in, authority.NextIntervention);
        authority.Interventions.resize(Count(in, 4));
        for (auto &intervention : authority.Interventions)
            ReadAll(in, intervention.Id, intervention.ExpiresAtTick, intervention.Scope.Origin.X, intervention.Scope.Origin.Y,
                    intervention.Scope.Width, intervention.Scope.Height);
        std::string knowledge, will;
        if (!(in >> std::quoted(knowledge) >> std::quoted(will))) throw WorldStorageError("missing social checkpoint");
        state.Knowledge = DecodeKnowledgeState(knowledge);
        state.WorldWill = DecodeWorldWillState(will);
    }
    if (version >= 5) {
        auto &population = state.Population;
        Read(in, population.Version);
        if (population.Version > 1) throw WorldStorageError("unsupported regional population version");
        population.Decisions.resize(Count(in, RegionalPopulation::MaximumSites));
        population.Dormant.resize(Count(in, RegionalPopulation::MaximumSites));
        // Pure codec round-trips of old saves retain the pending migration
        // marker until GameSession can bind the fixed legacy scene candidates.
        if (population.Version == 0 && (!population.Decisions.empty() || !population.Dormant.empty()))
            throw WorldStorageError("legacy population marker contains new records");
        for (auto &entry : population.Decisions) {
            unsigned kind;
            ReadAll(in, entry.Site.AnchorX, kind);
            if (kind != uint32_t(ActorKind::Resident) && kind != uint32_t(ActorKind::Monster))
                throw WorldStorageError("invalid regional population kind");
            entry.Site.Kind = ActorKind(kind);
            if (!(in >> std::quoted(entry.Site.Species)) || entry.Site.Species.size() > 32)
                throw WorldStorageError("invalid population species");
            Read(in, entry.Actor.Value);
        }
        for (auto &actor : population.Dormant) actor = ReadActor(in, version);
    } else {
        state.Population.Version = 0;
    }
    if (version >= 6) {
        ReadAll(in, state.Inventory.NextInstance, state.GroundBag.Value);
        state.DroppedItems.resize(Count(in, items::Inventory::MaximumInstancesPerContainer));
        for (auto &drop : state.DroppedItems) {
            uint32_t kind;
            ReadAll(in, kind, drop.Item.Instance);
            drop.Item.Item = items::ItemId(kind);
            drop.Position = ReadPosition(in);
        }
        for (auto &container : state.Inventory.Containers) {
            container.Instances.resize(Count(in, items::Inventory::MaximumInstancesPerContainer));
            for (auto &item : container.Instances) {
                uint32_t kind;
                std::string encoded;
                ReadAll(in, kind, item.Ref.Instance, item.FormulaVersion);
                item.Ref.Item = items::ItemId(kind);
                if (!(in >> std::quoted(encoded)) || encoded.size() > 32768)
                    throw WorldStorageError("invalid saved item formula");
                try {
                    checkpoint::Reader reader(encoded);
                    item.Formula = checkpoint::ReadProgram(reader);
                    reader.End();
                } catch (const std::invalid_argument &error) {
                    throw WorldStorageError(error.what());
                }
            }
        }
    }
    in >> std::ws;
    if (!in.eof()) throw WorldStorageError("unexpected gameplay checkpoint data");
    return state;
}
} // namespace pixel
