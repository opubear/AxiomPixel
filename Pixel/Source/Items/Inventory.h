#pragma once
#include "Item.h"
#include <compare>
#include <cstddef>
#include <map>
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
struct ContainerSnapshot {
    ContainerRef Ref;
    uint32_t Capacity = 0, Quantity = 0;
    std::vector<Stack> Stacks;
    bool operator==(const ContainerSnapshot &) const = default;
};

// Single writer. Reservations protect existing stock; it still occupies capacity.
// Cross-domain callers reserve, preflight their other domain, then consume/release.
class Inventory {
  public:
    static constexpr size_t MaximumContainers = 256, MaximumReservations = 64;
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
    ReservationResult Reserve(ContainerRef container, std::span<const ItemAmount> amounts);
    Result Release(ReservationId reservation);
    Result Consume(ReservationId reservation);

  private:
    struct Container {
        uint64_t Version = 1;
        uint32_t Capacity = 0, Quantity = 0;
        std::map<ItemId, Stack> Stacks;
    };
    struct Reservation { ContainerId Container; std::vector<ItemAmount> Amounts; };
    uint64_t Owner, NextContainer = 1, NextReservation = 1;
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
