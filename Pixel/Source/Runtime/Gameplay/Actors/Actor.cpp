#include "Actor.h"
#include "BodyComponent.h"
#include "FormulaComponent.h"
#include "MonsterComponent.h"
#include "HealthComponent.h"

namespace pixel {
Actor::Actor(const ActorRecord &state, std::shared_ptr<const MonsterCatalog> monsters)
    : Id(state.Id), Kind(state.Kind), Monsters(std::move(monsters)) {
    if (!Id.Value || Id.Value == UINT64_MAX)
        throw std::invalid_argument("runtime Actor requires a valid identity");
    if (Kind != ActorKind::Player && Kind != ActorKind::Resident && Kind != ActorKind::TrainingDummy && Kind != ActorKind::Monster)
        throw std::invalid_argument("unknown actor kind");
    if ((Kind == ActorKind::Monster) != state.Monster.has_value())
        throw std::invalid_argument("monster actor requires monster state");
    auto body = state.Body;
    if (state.Monster) {
        if (!Monsters || !state.Health) throw std::invalid_argument("monster requires catalog and health");
        const auto &definition = Monsters->Require(state.Monster->Definition);
        if (state.Health->Maximum != definition.MaxHealth || (state.Health->Current == 0) != (state.Monster->Action == MonsterAction::Death))
            throw std::invalid_argument("monster health and behavior disagree");
        body.Size = {CellsToCentimeters(definition.BodySizeCells[0]), CellsToCentimeters(definition.BodySizeCells[1])};
    }
    for (const auto size : body.Size)
        if (!std::isfinite(size) || size < 25 || size > 400) throw std::invalid_argument("invalid actor body size");
    if (Kind == ActorKind::Player && state.Health && state.Health->Current <= 0)
        throw std::invalid_argument("dead player must respawn before publication");
    const auto motion = Kind == ActorKind::TrainingDummy ? BodyMotion::Anchored : BodyMotion::Dynamic;
    AddComponent<BodyComponent>(body, motion);
    if (state.Health) AddComponent<HealthComponent>(*state.Health);
    if (state.Monster) AddComponent<MonsterComponent>(Monsters->Require(state.Monster->Definition), *state.Monster);
    if (state.Formula) EquipFormula(*state.Formula);
}

bool Actor::Step(WorldMapView terrain, PlayerInput input) {
    const bool advanced = GetComponent<BodyComponent>()->Step(terrain, input);
    if (advanced)
        if (auto *health = GetComponent<HealthComponent>()) health->Step();
    return advanced;
}

bool Actor::TakeDamage(float damage) {
    auto *health = GetComponent<HealthComponent>();
    if (!health || !health->Damage(damage)) return false;
    if (auto *monster = GetComponent<MonsterComponent>()) {
        monster->Hit(health->GetState().Current == 0);
    } else if (Kind == ActorKind::Player) {
        if (health->GetState().Current == 0) {
            GetComponent<BodyComponent>()->SetPosition(health->GetState().Respawn);
            health->Revive();
        } else {
            health->Protect(45);
        }
    }
    return true;
}

void Actor::EquipFormula(FormulaEquipment equipment) {
    if (auto *component = GetComponent<FormulaComponent>())
        component->Equip(equipment.Program);
    else
        AddComponent<FormulaComponent>(equipment.Program);
}

ActorRecord Actor::CaptureState() const {
    ActorRecord record{Id, Kind, GetComponent<BodyComponent>()->GetBody()};
    if (const auto *component = GetComponent<FormulaComponent>())
        record.Formula = FormulaEquipment{component->GetProgram()};
    if (const auto *component = GetComponent<MonsterComponent>()) record.Monster = component->GetState();
    if (const auto *component = GetComponent<HealthComponent>()) record.Health = component->GetState();
    return record;
}
} // namespace pixel
