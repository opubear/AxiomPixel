#include "Items/Inventory.h"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel::items;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class Function> void ContentError(Function function) {
    try { function(); } catch (const std::logic_error &) { return; }
    throw std::runtime_error("invalid content did not fail early");
}
void TransferAndVersions() {
    Inventory inventory;
    const auto source = inventory.Create(20), target = inventory.Create(6);
    const std::array stock{ItemAmount{ItemId::Stone, 8}, ItemAmount{ItemId::CopperOre, 4}};
    Check(inventory.Add(inventory.Reference(source), stock) == Result::Applied, "seed failed");
    const auto original = inventory.Inspect(source), empty = inventory.Inspect(target);
    const std::array tooMany{ItemAmount{ItemId::Stone, 7}};
    Check(inventory.Transfer(original.Ref, empty.Ref, tooMany) == Result::CapacityExceeded, "capacity not enforced");
    const std::array shortStock{ItemAmount{ItemId::Stone, 1}, ItemAmount{ItemId::IronOre, 1}};
    Check(inventory.Transfer(original.Ref, empty.Ref, shortStock) == Result::InsufficientItems, "partial debit accepted");
    Check(inventory.Inspect(source) == original && inventory.Inspect(target) == empty, "refusal mutated quantities/versions");

    const std::array cost{ItemAmount{ItemId::Stone, 4}, ItemAmount{ItemId::CopperOre, 2}};
    Check(inventory.Transfer(original.Ref, empty.Ref, cost) == Result::Applied, "transfer failed");
    Check(inventory.Inspect(source).Quantity == 6 && inventory.Inspect(target).Quantity == 6, "transfer lost stock");
    Check(inventory.Transfer(original.Ref, inventory.Reference(target), cost) == Result::StaleVersion, "stale source accepted");
    Check(inventory.Transfer(inventory.Reference(source), empty.Ref, cost) == Result::StaleVersion, "stale target accepted");
    Check(inventory.Transfer(inventory.Reference(source), inventory.Reference(source), cost) == Result::InvalidAmount,
          "self transfer accepted");
}
void Reservations() {
    Inventory inventory;
    const auto bag = inventory.Create(6), other = inventory.Create(10);
    const std::array cost{ItemAmount{ItemId::Stone, 4}, ItemAmount{ItemId::CopperOre, 2}};
    inventory.Add(inventory.Reference(bag), cost);
    const auto original = inventory.Inspect(bag);
    const auto reserved = inventory.Reserve(original.Ref, cost);
    Check(reserved.Status == Result::Applied, "reservation failed");
    Check(inventory.Reserve(original.Ref, cost).Status == Result::StaleVersion, "reservation reused stale version");
    const auto held = inventory.Inspect(bag);
    Check(held.Quantity == 6 && held.Stacks[0].Available() == 0 && held.Stacks[1].Available() == 0, "reservation removed physical stock");
    Check(inventory.Transfer(held.Ref, inventory.Reference(other), cost) == Result::InsufficientItems, "reserved stock was transferable");
    const std::array one{ItemAmount{ItemId::Stone, 1}};
    Check(inventory.Add(held.Ref, one) == Result::CapacityExceeded, "reservation freed physical capacity");
    Check(inventory.Release(reserved.Id) == Result::Applied, "release failed");
    Check(inventory.Inspect(bag).Stacks == original.Stacks, "release changed quantities");
    Check(inventory.Consume(reserved.Id) == Result::MissingReservation, "released token could consume");

    const auto paid = inventory.Reserve(inventory.Reference(bag), cost);
    Inventory another;
    Check(another.Consume(paid.Id) == Result::MissingReservation, "token crossed inventory owner");
    Check(inventory.Consume(paid.Id) == Result::Applied && inventory.Inspect(bag).Quantity == 0, "consumption failed");
    Check(inventory.Consume(paid.Id) == Result::MissingReservation && inventory.Release(paid.Id) == Result::MissingReservation,
          "reservation completed twice");
    inventory.Add(inventory.Reference(bag), one);
    const auto pending = inventory.Reserve(inventory.Reference(bag), one);
    const auto old = inventory.Reference(bag);
    inventory.Clear();
    const auto replacement = inventory.Create(10);
    Check(replacement != bag && inventory.Add(old, one) == Result::MissingContainer &&
          inventory.Release(pending.Id) == Result::MissingReservation, "clear resurrected old references");
}
void LimitsAndContent() {
    ContentError([] { Definition(ItemId(999)); });
    const std::array duplicate{ItemDefinition{ItemId::Soil, "soil"}, ItemDefinition{ItemId::Soil, "other"}};
    ContentError([&] { ValidateDefinitions(duplicate); });
    const std::array invalidCost{ItemAmount{ItemId(999), 1}};
    ContentError([&] { ValidateCost(invalidCost); });
    Inventory inventory;
    const auto bag = inventory.Create(UINT32_MAX);
    const std::array huge{ItemAmount{ItemId::Stone, UINT32_MAX}};
    Check(inventory.Add(inventory.Reference(bag), huge) == Result::Applied, "large quantity failed");
    const auto full = inventory.Inspect(bag);
    const std::array one{ItemAmount{ItemId::CopperOre, 1}};
    Check(inventory.Add(full.Ref, one) == Result::CapacityExceeded && inventory.Inspect(bag) == full, "quantity overflow");
    Check(inventory.Add(full.Ref, invalidCost) == Result::UnknownItem, "unknown item accepted");
    const std::array duplicateAmounts{ItemAmount{ItemId::Stone, 1}, ItemAmount{ItemId::Stone, 1}};
    Check(inventory.Reserve(full.Ref, duplicateAmounts).Status == Result::InvalidAmount, "duplicate entries bypassed availability");
    const std::array single{ItemAmount{ItemId::Stone, 1}};
    for (size_t i = 0; i < Inventory::MaximumReservations; ++i)
        Check(inventory.Reserve(inventory.Reference(bag), single).Status == Result::Applied, "reservation bound too small");
    const auto atLimit = inventory.Inspect(bag);
    Check(inventory.Reserve(atLimit.Ref, single).Status == Result::ReservationLimit && inventory.Inspect(bag) == atLimit,
          "reservation bound mutated inventory");
    for (size_t i = 1; i < Inventory::MaximumContainers; ++i) Check(inventory.Create(1).Value != 0, "container limit too small");
    Check(inventory.Create(1).Value == 0, "container bound ignored");
}
} // namespace
int main() {
    try {
        TransferAndVersions(); Reservations(); LimitsAndContent();
        std::cout << "Pixel items passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
