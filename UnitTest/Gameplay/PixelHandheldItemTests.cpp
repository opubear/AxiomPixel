#include "../Support/TestAccess.h"
#include "Runtime/Gameplay/Items/HandheldItem.h"
#include "Runtime/Gameplay/UI/InventoryUI.h"
#include "Runtime/Gameplay/FormulaBridge/DefaultProcess.h"
#include "Runtime/Render/GamePresentation.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace pixel;
using namespace pixel::items;
void Check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }

api::ActionReceipt Use(GameSession &session, std::optional<ItemRef> item, api::Position target = {{540, 510}, {0, 0}}) {
    return session.Execute(NativeCaller::Input, api::UseHeldItem{item, target});
}

void DropPickupUseAndPublication() {
    GameSession session;
    GameCamera camera;
    GamePresentation presentation;
    camera.SetFramebufferSize({1280, 720});
    camera.Reset(session.GetWorld().GetSimulationBounds(), session.GetPlayer());
    auto publish = presentation.CreateFramePublisher(session, camera);
    WorldGridFrame frame;
    ui::InventoryUI ui;
    Check(!session.GetActors().Find(session.GetActors().PlayerSnapshot()->Id)->Formula,
          "player still owns the default formula");
    Check(Use(session, {}).Rejection == api::Reason::InsufficientItems && session.GetFormulaRuntime().Stats().Roots == 0,
          "empty hand cast a formula");
    Check(session.Execute(NativeCaller::Input, api::ActivateProgram{session.AppliedProgram(), {{540, 510}, {0, 0}}}).Rejection
        == api::Reason::PermissionDenied, "input bypassed the held item with the workbench program");
    Check(session.DropWoodenStick(NativeCaller::Input).Rejection == api::Reason::PermissionDenied,
          "input acquired the debug drop command");
    Check(session.DropWoodenStick(NativeCaller::Debug).Succeeded(), "debug drop failed");
    const auto drop = session.GetDroppedItems().front();
    session.Tick(1.f / 60, {});
    Check(session.GetDroppedItems().size() == 1 && session.GetInventory().Inspect(session.PlayerInventory()).Quantity == 0,
          "drop was granted directly to the inventory");
    Check(Use(session, drop.Item).Rejection == api::Reason::InsufficientItems, "unowned ground item was usable");
    publish(frame);
    Check(frame.ItemDraws.size() == 3 && frame.ItemDraws.front().InstanceId == drop.Item.Instance,
          "committed drop has no visible world representation");
    const auto frozen = *presentation.PublishedGameplay();
    for (int i = 0; i < 12; ++i) session.Tick(1.f / 60, {1, false});
    Check(session.GetDroppedItems().empty() && session.GetInventory().ContainsItem(session.PlayerInventory(), drop.Item),
          "walking near the stick did not pick it up");
    publish(frame);
    Check(frame.ItemDraws.empty() && frozen.DroppedItems.size() == 1, "pickup mutated an old frame or left the item visible");
    ui.Publish(&*presentation.PublishedGameplay(), session.PlayerInventory());
    Check(ui.HeldItem() == drop.Item, "newly picked-up stick is not selected in the hotbar");
    const auto before = session.GetInventory().Inspect(session.PlayerInventory());
    const auto receipt = Use(session, ui.HeldItem());
    Check(receipt.Applied() && session.GetFormulaRuntime().Projectiles().size() == 1,
          "held stick did not cast its default formula");
    Check(std::get<api::ActivationResult>(receipt.Result).Program == ItemProgram(before.Instances.front())
        && session.GetInventory().Inspect(session.PlayerInventory()) == before, "cast consumed or misidentified the stick");
    ui.SelectQuickSlot(6);
    Check(!ui.HeldItem() && Use(session, ui.HeldItem()).Rejection == api::Reason::InsufficientItems,
          "selecting an empty slot kept the stick active");
    session.Control(NativeCaller::Driver, api::SetPaused{true});
    Check(Use(session, drop.Item).Rejection == api::Reason::GamePaused
        && session.DropWoodenStick(NativeCaller::Debug).Rejection == api::Reason::GamePaused, "paused item mutation succeeded");
}

void SharedBlockBehavior() {
    GameSession session;
    auto &inventory = testing::SessionAccess::Inventory(session);
    auto &world = testing::SessionAccess::World(session);
    for (const auto &definition : Definitions()) {
        if (!definition.BlockMaterial) continue;
        const std::array stock{ItemAmount{definition.Id, 2}};
        Check(inventory.Add(inventory.Reference(session.PlayerInventory()), stock) == Result::Applied, "block stock failed");
        const ItemRef item{definition.Id};
        auto behavior = MakeHandheldItem(inventory, session.PlayerInventory(), item);
        Check(dynamic_cast<BlockItem *>(behavior.get()), "block has a separate behavior class");
        const auto placed = Use(session, item);
        Check(placed.Applied() && world.GetCell(513, 510).Type == CellTypeId(definition.BlockMaterial)
            && world.GetCell(540, 510).Type == CellTypeId::Air,
              "held material was not placed directly in front");
        const auto before = inventory.Inspect(session.PlayerInventory());
        Check(Use(session, item).Rejection == api::Reason::Blocked && inventory.Inspect(session.PlayerInventory()) == before,
              "blocked placement consumed inventory");
        world.SetCell(513, 510, {});
        const auto left = Use(session, item, {{490, 510}, {0, 0}});
        Check(left.Applied() && world.GetCell(510, 510).Type == CellTypeId(definition.BlockMaterial), "left-facing placement failed");
        world.SetCell(510, 510, {});
        Check(Use(session, item).Rejection == api::Reason::InsufficientItems, "depleted block remained usable");
    }
}

void CheckBlockPlacementBesidePlayer(GameSession &session, bool left, api::Cell expected) {
    auto &inventory = testing::SessionAccess::Inventory(session);
    auto &world = testing::SessionAccess::World(session);
    const std::array stock{ItemAmount{ItemId::Stone, 2}};
    Check(inventory.Add(inventory.Reference(session.PlayerInventory()), stock) == Result::Applied, "fractional block stock failed");
    const auto before = inventory.Inspect(session.PlayerInventory());
    const ItemRef stone{ItemId::Stone};
    const api::Position aim{{left ? 490 : 540, 510}, {0, 0}};
    const auto placed = Use(session, stone, aim);
    const auto *result = std::get_if<api::MaterialResult>(&placed.Result);
    Check(placed.Applied() && result && result->Target == expected
        && world.GetCell(expected.X, expected.Y).Type == CellTypeId::Stone
        && !session.GetActors().Occupies({expected.X, expected.Y}),
          "block placement between cells did not select the adjacent unoccupied cell");
    const auto after = inventory.Inspect(session.PlayerInventory());
    Check(after.Quantity + 1 == before.Quantity, "fractional placement did not consume exactly one block");
    Check(Use(session, stone, aim).Rejection == api::Reason::Blocked
        && inventory.Inspect(session.PlayerInventory()) == after
        && world.GetCell(expected.X, expected.Y).Type == CellTypeId::Stone,
          "occupied fractional placement changed terrain or consumed stock");
    world.SetCell(expected.X, expected.Y, {});
}

void BlockPlacementBetweenCells() {
    struct Case { double Offset; int64_t Left, Right; };
    const std::array cases{
        Case{0, 510, 513}, Case{.0001, 510, 514}, Case{.1, 510, 514}, Case{.25, 510, 514},
        Case{.4999, 510, 514}, Case{.5, 510, 514}, Case{.5001, 510, 514}, Case{.75, 510, 514},
        Case{.9999, 510, 514}, Case{1, 511, 514}};
    GameSession session;
    for (const auto &sample : cases) {
        testing::SessionAccess::SetPlayerPosition(session, WorldPosition::FromCell({511, 508}, {sample.Offset, 0}));
        CheckBlockPlacementBesidePlayer(session, false, {sample.Right, 510});
        CheckBlockPlacementBesidePlayer(session, true, {sample.Left, 510});
    }
}

void BlockPlacementAfterWalking() {
    for (const int direction : {-1, 1}) {
        for (const int steps : {1, 2, 3}) {
            GameSession session;
            for (int step = 0; step < steps; ++step) session.Tick(1.f / 60, {direction, false});
            CheckBlockPlacementBesidePlayer(session, false, {direction < 0 ? 513 : 514, 510});
            CheckBlockPlacementBesidePlayer(session, true, {direction < 0 ? 509 : 510, 510});
        }
    }
}

void IndependentProgramsAndCheckpoint() {
    GameSession session;
    auto &inventory = testing::SessionAccess::Inventory(session);
    ItemRef first, second;
    inventory.CreateStick(inventory.Reference(session.PlayerInventory()), DefaultPlayerProcess(), first);
    inventory.CreateStick(inventory.Reference(session.PlayerInventory()), DefaultPlayerProcess(), second);
    const auto changed = std::get<formula::process::ProgramArtifact>(formula::process::Compile(
        "stick-test", "formula 2; run bolt(7, speed(0), ticks(100));"));
    const auto original = *inventory.FindInstance(session.PlayerInventory(), second);
    Check(bool(session.ApplyHeldProcess(NativeCaller::Debug, first, changed)), "stick editing failed");
    Check(*inventory.FindInstance(session.PlayerInventory(), second) == original, "editing one stick changed another");
    Check(Use(session, first).Applied() && session.GetFormulaRuntime().Projectiles().back().Damage == 7, "edited stick used the workbench program");
    const auto oldCast = session.GetFormulaRuntime().InspectProcesses().front();
    Check(bool(session.ApplyHeldProcess(NativeCaller::Debug, first, DefaultPlayerProcess())), "second edit failed");
    Check(session.GetFormulaRuntime().InspectProcesses().front().Version == oldCast.Version, "editing changed an existing cast");
    Check(session.DropWoodenStick(NativeCaller::Debug).Succeeded(), "checkpoint ground item failed");
    const auto saved = testing::SessionAccess::CaptureGameplay(session);
    const auto decoded = DecodeGameplayState(EncodeGameplayState(saved));
    GameSession restored;
    testing::SessionAccess::RestoreGameplay(restored, decoded, session.CurrentCommit().WorldId);
    Check(restored.GetInventory().Inspect(restored.PlayerInventory()) == inventory.Inspect(session.PlayerInventory())
        && restored.GetDroppedItems().front().Item == session.GetDroppedItems().front().Item,
          "checkpoint lost item identity, formula or location");
    restored.BeginFrame();
    Check(Use(restored, second).Applied(), "restored stick cannot cast");
    auto corrupt = decoded;
    corrupt.DroppedItems.front().Item = first;
    bool rejected = false;
    try { GameSession invalid; testing::SessionAccess::RestoreGameplay(invalid, corrupt, session.CurrentCommit().WorldId); }
    catch (const std::exception &) { rejected = true; }
    Check(rejected, "checkpoint accepted an item both in the bag and on the ground");
    session.Control(NativeCaller::Driver, api::ResetWorld{});
    Check(session.GetDroppedItems().empty() && Use(session, first).Rejection == api::Reason::InsufficientItems,
          "reset retained an old item handle");
}

void FullBagAndQueuedUse() {
    GameSession session;
    auto &inventory = testing::SessionAccess::Inventory(session);
    Check(session.DropWoodenStick(NativeCaller::Debug).Succeeded(), "capacity fixture drop failed");
    const auto drop = session.GetDroppedItems().front();
    const std::array fill{ItemAmount{ItemId::Soil, 4096}};
    inventory.Add(inventory.Reference(session.PlayerInventory()), fill);
    for (int i = 0; i < 12; ++i) session.Tick(1.f / 60, {1, false});
    Check(session.GetDroppedItems().size() == 1 && inventory.Inspect(session.PlayerInventory()).Quantity == 4096,
          "full bag lost a dropped item");
    const std::array one{ItemAmount{ItemId::Soil, 1}};
    inventory.Consume(inventory.Reserve(inventory.Reference(session.PlayerInventory()), one).Id);
    session.Tick(1.f / 60, {});
    Check(session.GetDroppedItems().empty(), "pickup did not retry after capacity was freed");
    session.EnableSimulationThread();
    const auto queued = Use(session, drop.Item);
    Check(queued.Queued() && session.GetFormulaRuntime().Projectiles().empty(), "queued use ran before the simulation step");
    session.Tick(1.f / 60, {});
    Check(session.FindAction(queued.Key)->Applied() && session.GetFormulaRuntime().Projectiles().size() == 1,
          "queued held-item use was lost or repeated");
}

void LegacyProgramMigration() {
    for (const bool full : {false, true}) {
        GameSession original;
        const auto program = std::get<formula::process::ProgramArtifact>(formula::process::Compile(
            "legacy-player", "formula 2; run bolt(11, speed(0), ticks(100));"));
        Check(bool(original.ApplyProcess(NativeCaller::Driver, program)), "legacy program fixture failed");
        auto &inventory = testing::SessionAccess::Inventory(original);
        if (full) {
            const std::array stock{ItemAmount{ItemId::Soil, 4096}};
            inventory.Add(inventory.Reference(original.PlayerInventory()), stock);
        }
        auto state = testing::SessionAccess::CaptureGameplay(original);
        std::erase_if(state.Inventory.Containers, [&](const auto &container) { return container.Ref.Id == state.GroundBag; });
        state.GroundBag = {};
        auto encoded = EncodeGameplayState(state);
        // A v5 payload ends before the v6 instance watermark and empty tool list.
        const std::string addedSection = "1 0 0\n0\n";
        Check(encoded.ends_with(addedSection), "unexpected legacy fixture layout");
        encoded.resize(encoded.size() - addedSection.size());
        encoded.replace(0, std::string("PixelGameplay 6").size(), "PixelGameplay 5");
        const auto legacy = DecodeGameplayState(encoded);
        GameSession restored;
        testing::SessionAccess::RestoreGameplay(restored, legacy, original.CurrentCommit().WorldId);
        Check(!restored.GetActors().Find(restored.GetActors().PlayerSnapshot()->Id)->Formula,
              "legacy restore rebuilt the player formula component");
        if (full) {
            Check(restored.GetDroppedItems().size() == 1, "full legacy bag lost the migrated program");
            auto &bag = testing::SessionAccess::Inventory(restored);
            const std::array one{ItemAmount{ItemId::Soil, 1}};
            bag.Consume(bag.Reserve(bag.Reference(restored.PlayerInventory()), one).Id);
            restored.Tick(1.f / 60, {});
        }
        const auto bag = restored.GetInventory().Inspect(restored.PlayerInventory());
        Check(bag.Instances.size() == 1 && bag.Instances.front().Formula->Identity() == program.Identity()
            && restored.GetDroppedItems().empty(), "legacy formula did not migrate to one owned stick");
        restored.BeginFrame();
        Check(Use(restored, bag.Instances.front().Ref).Applied()
            && restored.GetFormulaRuntime().Projectiles().back().Damage == 11, "migrated formula changed behavior");
    }
}
} // namespace

int main() {
    try {
        DropPickupUseAndPublication(); SharedBlockBehavior(); BlockPlacementBetweenCells(); BlockPlacementAfterWalking();
        IndependentProgramsAndCheckpoint(); FullBagAndQueuedUse();
        LegacyProgramMigration();
        std::cout << "Held items: pickup, selection, per-item formulas, blocks at fractional and walking positions, checkpoint, legacy migration and queued use passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
