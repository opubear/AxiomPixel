#pragma once
#include "ActorState.h"
#include "Component.h"
#include "PlayerInput.h"
#include "MonsterCatalog.h"
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace pixel {
// Gameplay objects with an independent identity and behavior own their
// capabilities as components, whether player-controlled, autonomous or anchored.
class Actor {
  public:
    explicit Actor(const ActorRecord &state, std::shared_ptr<const MonsterCatalog> monsters = {});
    Actor(const Actor &) = delete;
    Actor &operator=(const Actor &) = delete;

    ActorId GetId() const { return Id; }
    ActorKind GetKind() const { return Kind; }
    size_t ComponentCount() const { return Components.size(); }

    template<class T, class... Args> T &AddComponent(Args &&...args) {
        static_assert(std::is_base_of_v<Component, T> && !std::is_same_v<Component, T>);
        if (GetComponent<T>()) throw std::logic_error("duplicate actor component");
        auto component = std::make_unique<T>(*this, std::forward<Args>(args)...);
        auto &result = *component;
        Components.push_back(std::move(component));
        return result;
    }

    template<class T> T *GetComponent() {
        static_assert(std::is_base_of_v<Component, T>);
        for (auto &component : Components)
            if (auto *typed = dynamic_cast<T *>(component.get())) return typed;
        return nullptr;
    }

    template<class T> const T *GetComponent() const {
        static_assert(std::is_base_of_v<Component, T>);
        for (const auto &component : Components)
            if (auto *typed = dynamic_cast<const T *>(component.get())) return typed;
        return nullptr;
    }

    // Non-monster motion and health timers. Actors coordinates autonomous
    // monster steps; formulas activate through the session action boundary.
    bool Step(WorldMapView terrain, PlayerInput input);
    void EquipFormula(FormulaEquipment equipment);
    ActorRecord CaptureState() const;
    bool TakeDamage(float damage);

  private:
    ActorId Id;
    ActorKind Kind;
    std::shared_ptr<const MonsterCatalog> Monsters;
    std::vector<std::unique_ptr<Component>> Components;
};
} // namespace pixel
