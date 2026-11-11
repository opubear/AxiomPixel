#pragma once

namespace pixel {
class Actor;

// Components belong to one stable Actor for their entire lifetime. They are
// runtime objects; checkpoints and published frames contain values instead.
class Component {
  public:
    virtual ~Component() = default;
    Component(const Component &) = delete;
    Component &operator=(const Component &) = delete;
    const Actor &GetOwner() const { return Owner; }

  protected:
    explicit Component(Actor &owner) : Owner(owner) {}

  private:
    Actor &Owner;
};
} // namespace pixel
