#pragma once
#include "Item.h"
#include "Formula/Process/Program.h"
#include <compare>
#include <cstddef>
#include <map>
#include <optional>
#include <vector>

namespace pixel::items {
struct ContainerId { uint64_t Value = 0; auto operator<=>(const ContainerId &) const = default; };
struct ContainerRef {
    ContainerId Id;
    uint64_t Version = 0;
    bool operator==(const ContainerRef &) const = default;
};
struct ReservationId {
    uint64_t Owner = 0, Value = 0;
    bool operator==(const ReservationId &) const = default;
};
enum class Result { Applied, MissingContainer, StaleVersion, InvalidAmount, UnknownItem,
                    InsufficientItems, CapacityExceeded, ReservationLimit, MissingReservation };
struct ReservationResult { Result Status; ReservationId Id; };
struct Stack {
    ItemId Item;
    uint32_t Quantity = 0, Reserved = 0;
    uint32_t Available() const { return Quantity - Reserved; }
    bool operator==(const Stack &) const = default;
};
struct ItemInstance {
    ItemRef Ref;
    uint64_t FormulaVersion = 1;
    std::optional<formula::process::ProgramArtifact> Formula;
    bool operator==(const ItemInstance &other) const {
        return Ref == other.Ref && FormulaVersion == other.FormulaVersion
            && Formula.has_value() == other.Formula.has_value()
            && (!Formula || Formula->Identity() == other.Formula->Identity());
    }
};
struct ContainerSnapshot {
    ContainerRef Ref;
    uint32_t Capacity = 0, Quantity = 0;
    std::vector<Stack> Stacks;
    std::vector<ItemInstance> Instances;
    bool operator==(const ContainerSnapshot &) const = default;
};
struct InventoryState {
    uint64_t NextContainer = 1, NextReservation = 1;
    std::vector<ContainerSnapshot> Containers;
    uint64_t NextInstance = 1;
};

// Single writer. Reservations protect existing stock; it still occupies capacity.
// Cross-domain callers reserve, preflight their other domain, then consume/release.
class Inventory {
  public:
    static constexpr size_t MaximumContainers = 256, MaximumReservations = 64;
    // Fourteen resource entries plus these tools fit the current 35-cell bag.
    static constexpr size_t MaximumInstancesPerContainer = 21;
    static constexpr uint64_t InstanceResourceBase = uint64_t{1} << 63;
    Inventory();
    Inventory(const Inventory &) = delete;
    Inventory &operator=(const Inventory &) = delete;
    ContainerId Create(uint32_t capacity);
    size_t Count() const { return Containers.size(); }
    void Destroy(ContainerId container); // Trusted owner; must be empty and unreserved.
    void Clear(); // Expires all references without reusing container/reservation IDs.
    ContainerSnapshot Inspect(ContainerId container) const;
    ContainerRef Reference(ContainerId container) const;
    Result Add(ContainerRef container, std::span<const ItemAmount> amounts);
    Result Transfer(ContainerRef source, ContainerRef destination, std::span<const ItemAmount> amounts);
    Result CreateStick(ContainerRef destination, const formula::process::ProgramArtifact &formula, ItemRef &created);
    Result TransferInstance(ContainerRef source, ContainerRef destination, ItemRef item);
    const ItemInstance *FindInstance(ContainerId container, ItemRef item) const;
    bool ContainsItem(ContainerId container, ItemRef item) const;
    Result SetFormula(ContainerRef container, ItemRef item, const formula::process::ProgramArtifact &formula);
    ReservationResult Reserve(ContainerRef container, std::span<const ItemAmount> amounts);
    Result Release(ReservationId reservation);
    Result Consume(ReservationId reservation);
    InventoryState CaptureState() const;
    void RestoreState(const InventoryState &state);
    bool Contains(ContainerId id) const { return Containers.contains(id); }

  private:
    struct Container {
        uint64_t Version = 1;
        uint32_t Capacity = 0, Quantity = 0;
        std::map<ItemId, Stack> Stacks;
        std::map<uint64_t, ItemInstance> Instances;
    };
    struct Reservation { ContainerId Container; std::vector<ItemAmount> Amounts; };
    uint64_t Owner, NextContainer = 1, NextReservation = 1, NextInstance = 1;
    std::map<ContainerId, Container> Containers;
    std::map<uint64_t, Reservation> Reservations;
    Result Find(ContainerRef reference, Container *&container);
    static Result Validate(std::span<const ItemAmount> amounts);
    static Result Available(const Container &container, std::span<const ItemAmount> amounts);
    static uint64_t Total(std::span<const ItemAmount> amounts);
    static void Touch(Container &container);
    Result Finish(ReservationId reservation, bool consume);
};
} // namespace pixel::items
