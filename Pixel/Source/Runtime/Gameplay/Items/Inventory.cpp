#include "Inventory.h"
#include <atomic>
#include <stdexcept>
#include <set>

namespace pixel::items {
namespace {
uint64_t NextIdentity(uint64_t &next) {
    if (next == UINT64_MAX) throw std::overflow_error("inventory identity exhausted");
    return next++;
}
}
Inventory::Inventory() {
    static std::atomic<uint64_t> next{1};
    Owner = next.fetch_add(1);
    if (Owner == UINT64_MAX) std::terminate();
    ValidateDefinitions(Definitions());
}
ContainerId Inventory::Create(uint32_t capacity) {
    if (!capacity) throw std::invalid_argument("container capacity must be positive");
    if (Containers.size() == MaximumContainers) return {};
    const ContainerId id{NextIdentity(NextContainer)};
    Containers.emplace(id, Container{1, capacity});
    return id;
}
void Inventory::Clear() { Reservations.clear(); Containers.clear(); }

InventoryState Inventory::CaptureState() const {
    if (!Reservations.empty()) throw std::logic_error("checkpoint during inventory reservation");
    InventoryState state{NextContainer, NextReservation};
    state.NextInstance = NextInstance;
    for (const auto &[id, container] : Containers) state.Containers.push_back(Inspect(id));
    return state;
}

void Inventory::RestoreState(const InventoryState &state) {
    if (!state.NextContainer || !state.NextReservation || !state.NextInstance
        || state.NextInstance >= InstanceResourceBase || state.Containers.size() > MaximumContainers)
        throw std::invalid_argument("invalid inventory checkpoint");
    std::map<ContainerId, Container> restored;
    std::set<uint64_t> instances;
    for (const auto &saved : state.Containers) {
        if (!saved.Ref.Id.Value || saved.Ref.Id.Value >= state.NextContainer || !saved.Ref.Version
            || saved.Ref.Version == UINT64_MAX || !saved.Capacity || saved.Stacks.size() > Definitions().size()
            || saved.Instances.size() > MaximumInstancesPerContainer)
            throw std::invalid_argument("invalid saved container");
        Container container{saved.Ref.Version, saved.Capacity, saved.Quantity};
        uint64_t total = 0;
        for (const auto &stack : saved.Stacks) {
            if (!IsKnown(stack.Item) || !Definition(stack.Item).BlockMaterial || !stack.Quantity || stack.Reserved
                || !container.Stacks.emplace(stack.Item, stack).second)
                throw std::invalid_argument("invalid saved item stack");
            total += stack.Quantity;
        }
        for (const auto &item : saved.Instances) {
            if (item.Ref.Item != ItemId::WoodenStick || !item.Ref.Instance || item.Ref.Instance >= state.NextInstance
                || !item.Formula || !item.FormulaVersion || item.FormulaVersion == UINT64_MAX
                || !instances.insert(item.Ref.Instance).second)
                throw std::invalid_argument("invalid or duplicated saved item instance");
            container.Instances.emplace(item.Ref.Instance, item);
            ++total;
        }
        if (total != saved.Quantity || total > saved.Capacity || !restored.emplace(saved.Ref.Id, std::move(container)).second)
            throw std::invalid_argument("invalid saved inventory contents");
    }
    Clear();
    Inventory fresh;
    Owner = fresh.Owner;
    Containers = std::move(restored);
    NextContainer = state.NextContainer;
    NextReservation = state.NextReservation;
    NextInstance = state.NextInstance;
}
void Inventory::Destroy(ContainerId id) {
    if (Containers.at(id).Quantity != 0) throw std::logic_error("destroying nonempty inventory");
    Containers.erase(id);
}
ContainerSnapshot Inventory::Inspect(ContainerId id) const {
    const auto &container = Containers.at(id);
    ContainerSnapshot snapshot{{id, container.Version}, container.Capacity, container.Quantity};
    for (const auto &[item, stack] : container.Stacks) snapshot.Stacks.push_back(stack);
    for (const auto &[id, item] : container.Instances) snapshot.Instances.push_back(item);
    return snapshot;
}
ContainerRef Inventory::Reference(ContainerId id) const { return {id, Containers.at(id).Version}; }
Result Inventory::Find(ContainerRef reference, Container *&container) {
    const auto it = Containers.find(reference.Id);
    if (it == Containers.end()) return Result::MissingContainer;
    if (it->second.Version != reference.Version) return Result::StaleVersion;
    container = &it->second;
    return Result::Applied;
}
void Inventory::Touch(Container &container) {
    if (container.Version == UINT64_MAX) throw std::overflow_error("container version exhausted");
    ++container.Version;
}
Result Inventory::Validate(std::span<const ItemAmount> amounts) {
    if (amounts.empty() || amounts.size() > Definitions().size()) return Result::InvalidAmount;
    for (size_t i = 0; i < amounts.size(); ++i) {
        if (!IsKnown(amounts[i].Item)) return Result::UnknownItem;
        if (!amounts[i].Quantity || !Definition(amounts[i].Item).BlockMaterial) return Result::InvalidAmount;
        for (size_t j = 0; j < i; ++j)
            if (amounts[j].Item == amounts[i].Item) return Result::InvalidAmount;
    }
    return Result::Applied;
}
uint64_t Inventory::Total(std::span<const ItemAmount> amounts) {
    uint64_t total = 0;
    for (const auto &amount : amounts) total += amount.Quantity;
    return total;
}
Result Inventory::Available(const Container &container, std::span<const ItemAmount> amounts) {
    for (const auto &amount : amounts) {
        const auto it = container.Stacks.find(amount.Item);
        if (it == container.Stacks.end() || it->second.Available() < amount.Quantity)
            return Result::InsufficientItems;
    }
    return Result::Applied;
}
Result Inventory::Add(ContainerRef reference, std::span<const ItemAmount> amounts) {
    auto result = Validate(amounts);
    if (result != Result::Applied) return result;
    Container *container = nullptr;
    result = Find(reference, container);
    if (result != Result::Applied) return result;
    const auto total = Total(amounts);
    if (total > container->Capacity - container->Quantity) return Result::CapacityExceeded;

    for (const auto &amount : amounts) {
        auto &stack = container->Stacks.try_emplace(amount.Item, Stack{amount.Item}).first->second;
        stack.Quantity += amount.Quantity;
    }
    container->Quantity += uint32_t(total);
    Touch(*container);
    return Result::Applied;
}
Result Inventory::Transfer(ContainerRef source, ContainerRef destination, std::span<const ItemAmount> amounts) {
    auto result = Validate(amounts);
    if (result != Result::Applied) return result;
    Container *from = nullptr, *to = nullptr;
    result = Find(source, from);
    if (result != Result::Applied) return result;
    result = Find(destination, to);
    if (result != Result::Applied) return result;
    if (source.Id == destination.Id) return Result::InvalidAmount;
    result = Available(*from, amounts);
    if (result != Result::Applied) return result;
    const auto total = Total(amounts);
    if (total > to->Capacity - to->Quantity) return Result::CapacityExceeded;

    for (const auto &amount : amounts) {
        auto &target = to->Stacks.try_emplace(amount.Item, Stack{amount.Item}).first->second;
        target.Quantity += amount.Quantity;
        auto &origin = from->Stacks.at(amount.Item);
        origin.Quantity -= amount.Quantity;
        if (!origin.Quantity) from->Stacks.erase(amount.Item);
    }
    from->Quantity -= uint32_t(total);
    to->Quantity += uint32_t(total);
    Touch(*from);
    Touch(*to);
    return Result::Applied;
}
ReservationResult Inventory::Reserve(ContainerRef reference, std::span<const ItemAmount> amounts) {
    auto result = Validate(amounts);
    if (result != Result::Applied) return {result, {}};
    Container *container = nullptr;
    result = Find(reference, container);
    if (result != Result::Applied) return {result, {}};
    result = Available(*container, amounts);
    if (result != Result::Applied) return {result, {}};
    if (Reservations.size() == MaximumReservations) return {Result::ReservationLimit, {}};

    const auto id = NextIdentity(NextReservation);
    Reservations.emplace(id, Reservation{reference.Id, {amounts.begin(), amounts.end()}});
    for (const auto &amount : amounts) container->Stacks.at(amount.Item).Reserved += amount.Quantity;
    Touch(*container);
    return {Result::Applied, {Owner, id}};
}
Result Inventory::Finish(ReservationId id, bool consume) {
    if (id.Owner != Owner) return Result::MissingReservation;
    const auto it = Reservations.find(id.Value);
    if (it == Reservations.end()) return Result::MissingReservation;
    auto &container = Containers.at(it->second.Container);
    for (const auto &amount : it->second.Amounts) {
        auto &stack = container.Stacks.at(amount.Item);
        stack.Reserved -= amount.Quantity;
        if (consume) {
            stack.Quantity -= amount.Quantity;
            container.Quantity -= amount.Quantity;
        }
        if (!stack.Quantity) container.Stacks.erase(amount.Item);
    }
    Reservations.erase(it);
    Touch(container);
    return Result::Applied;
}
Result Inventory::CreateStick(ContainerRef destination, const formula::process::ProgramArtifact &formula, ItemRef &created) {
    Container *container = nullptr;
    const auto result = Find(destination, container);
    if (result != Result::Applied) return result;
    if (container->Quantity == container->Capacity || container->Instances.size() == MaximumInstancesPerContainer)
        return Result::CapacityExceeded;
    if (NextInstance >= InstanceResourceBase - 1) throw std::overflow_error("item instance identity exhausted");
    created = {ItemId::WoodenStick, NextIdentity(NextInstance)};
    container->Instances.emplace(created.Instance, ItemInstance{created, 1, formula});
    ++container->Quantity;
    Touch(*container);
    return Result::Applied;
}

const ItemInstance *Inventory::FindInstance(ContainerId id, ItemRef item) const {
    const auto &container = Containers.at(id);
    const auto found = container.Instances.find(item.Instance);
    return found != container.Instances.end() && found->second.Ref == item ? &found->second : nullptr;
}

bool Inventory::ContainsItem(ContainerId id, ItemRef item) const {
    if (item.Instance) return FindInstance(id, item) != nullptr;
    const auto &stacks = Containers.at(id).Stacks;
    const auto found = stacks.find(item.Item);
    return found != stacks.end() && found->second.Available() != 0;
}

Result Inventory::TransferInstance(ContainerRef source, ContainerRef destination, ItemRef item) {
    Container *from = nullptr, *to = nullptr;
    auto result = Find(source, from);
    if (result != Result::Applied) return result;
    result = Find(destination, to);
    if (result != Result::Applied) return result;
    if (source.Id == destination.Id) return Result::InvalidAmount;
    if (!FindInstance(source.Id, item)) return Result::InsufficientItems;
    if (to->Quantity == to->Capacity || to->Instances.size() == MaximumInstancesPerContainer)
        return Result::CapacityExceeded;

    to->Instances.insert(from->Instances.extract(item.Instance));
    --from->Quantity;
    ++to->Quantity;
    Touch(*from);
    Touch(*to);
    return Result::Applied;
}

Result Inventory::SetFormula(ContainerRef reference, ItemRef item, const formula::process::ProgramArtifact &formula) {
    Container *container = nullptr;
    const auto result = Find(reference, container);
    if (result != Result::Applied) return result;
    if (!FindInstance(reference.Id, item)) return Result::InsufficientItems;
    auto &instance = container->Instances.at(item.Instance);
    if (instance.FormulaVersion == UINT64_MAX - 1) throw std::overflow_error("item formula version exhausted");
    instance.Formula = formula;
    ++instance.FormulaVersion;
    Touch(*container);
    return Result::Applied;
}

Result Inventory::Release(ReservationId id) { return Finish(id, false); }
Result Inventory::Consume(ReservationId id) { return Finish(id, true); }
} // namespace pixel::items
