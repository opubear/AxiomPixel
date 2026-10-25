#include "Dev/Debug/FormulaEditing/FormulaDraft.h"
#include "Formula/Simulator.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <set>

using namespace pixel::formula;
using namespace pixel::formula::editing;
namespace {
void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

CompileResult Compiled(const Draft &draft) {
    auto result = Compile(draft);
    if (!result)
        throw std::runtime_error(result.Errors.empty() ? "missing compiled formula" : result.Errors.front().Message);
    return result;
}

void CollectIds(const DraftNode &node, std::set<NodeId> &ids) {
    Require(ids.insert(node.Id).second, "draft IDs must be unique");
    for (const auto &child : node.Children)
        CollectIds(*child, ids);
}

std::set<NodeId> Ids(const Draft &draft) {
    std::set<NodeId> ids;
    CollectIds(draft.Root(), ids);
    return ids;
}

template <class F>
void Invalid(F &&call, const char *message) {
    try {
        call();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error(message);
}

void StructuralEditing() {
    Draft draft;
    const auto original = draft.Root().Id;
    const auto delay = draft.Wrap(original, NodeKind::Delay);
    Require(delay != 0 && draft.Find(original), "wrapping preserves original node identity");
    draft.Find(delay)->Values.Ticks = 6;
    Require(Format(*Compiled(draft).Description) == "delay(6, bolt(10, 60, 120))", "wrap compiles through factory");
    Require(draft.Unwrap(delay) == original, "unwrap preserves child ID");
    const auto copy = draft.Duplicate(original);
    Require(copy != 0 && copy != original && draft.Root().Kind == NodeKind::Sequence, "duplicate root wraps a sequence");
    draft.Find(copy)->Values.Damage = 20;
    const auto root = draft.Root().Id;
    const auto wind = draft.AddChild(root, NodeKind::Wind);
    Require(draft.Move(wind, -1) && draft.Move(wind, -1) && !draft.Move(wind, -1), "sequence reorders with stable IDs");
    Require(draft.Root().Children[0]->Id == wind && draft.Find(copy)->Values.Damage == 20, "selection survives vector reorder");
    Require(draft.Unwrap(root) == 0, "unwrap must not discard sequence siblings");
    Require(draft.Delete(wind) == root && !draft.Find(wind), "sequence delete selects parent");
    Require(draft.Replace(copy, NodeKind::Dig) == root && !draft.Find(copy), "replace retires selected identity");
    const auto trigger = draft.Wrap(original, NodeKind::OnHit);
    const auto carrier = draft.Find(trigger)->Children[0]->Id;
    Require(draft.Duplicate(carrier) == 0, "fixed slots cannot gain arbitrary siblings");
    Require(draft.Delete(carrier) == trigger && draft.Find(trigger)->Children[0]->Kind == NodeKind::Empty,
            "fixed-slot delete inserts empty rather than breaking structure");
    std::set<NodeId> ids;
    CollectIds(draft.Root(), ids);
    const auto oldMaximum = *ids.rbegin();
    draft.FromFormula(Formula::Bolt());
    Require(draft.Root().Id > oldMaximum, "revert allocates fresh IDs without reuse");
    const auto oldRoot = draft.Root().Id;
    const auto empty = draft.Delete(oldRoot);
    Require(empty > oldRoot && draft.Root().Kind == NodeKind::Empty && !draft.Find(oldRoot), "root deletion produces fresh empty");
}

void RoundTripAndScope() {
    const auto source = Parse("cast seq(empty(), on_hit(on_hit(bolt() |> damage_add(5), dig(1)), on_hit(bolt(2, 3, 4), wind(2, 5))) |> damage_mul(3), repeat(2, 12, fan(3, 10, delay(1, bolt()))));");
    Draft draft;
    draft.FromFormula(source);
    const auto result = Compiled(draft);
    Require(Format(*result.Description) == Format(source), "all public node forms round-trip unchanged");
    Require(result.Budget.Effects == 10 && result.Budget.WindEffects == 1, "analysis includes stacked and nested hit descendants");
    Require(result.Budget.EndTick == 133, "end bound includes both delayed branches and hit descendants");
    Simulator sim;
    sim.Cast(*result.Description);
    sim.AdvanceTo(0);
    Require(sim.Projectiles().size() == 1 && sim.Projectiles()[0].Damage == 45, "carrier modifiers keep their order");
    Require(sim.Hit(sim.Projectiles()[0].Id, {}), "carrier hit accepted");
    sim.AdvanceTo(1);
    bool payloadFound = false;
    for (const auto &projectile : sim.Projectiles()) {
        if (projectile.Damage == 2) {
            payloadFound = true;
            Require(std::abs(std::hypot(projectile.Velocity.X, projectile.Velocity.Y) - 3) < 1e-9,
                    "payload does not inherit carrier modifiers");
        }
    }
    Require(payloadFound, "compiled nested trigger retains payload");
}

void ParameterAndStructureErrors() {
    Draft draft;
    const auto id = draft.Root().Id;
    draft.Root().Values.Lifetime = 0;
    auto result = Compile(draft);
    Require(!result && result.Errors[0].Node == id && result.Errors[0].Field == "lifetime", "errors locate node and field");
    draft.Root().Values.Lifetime = 600;
    draft.Root().Values.Speed = 1200;
    const auto modifier = draft.Wrap(id, NodeKind::MultiplySpeed);
    draft.Find(modifier)->Values.Factor = 0.5;
    Require(static_cast<bool>(Compile(draft)), "speed bound applies after modifiers");
    draft.Find(modifier)->Values.Factor = 1;
    Require(!Compile(draft), "evaluated speed above 600 rejected");
    draft.FromFormula(Formula::Bolt(std::numeric_limits<double>::max(), 0, 1).MultiplyDamage(2));
    Require(!Compile(draft), "nonfinite intermediate damage is rejected before execution");
    draft.FromFormula(Formula::Bolt().Spread(16, std::numeric_limits<double>::max()));
    Require(!Compile(draft), "nonfinite fan offset is rejected");
    draft.FromFormula(Formula::Wind(1e-300, -600));
    Require(static_cast<bool>(Compile(draft)), "tiny radii remain valid descriptions for adapter diagnostics");
    draft.Root().Values.Impulse = -601;
    Require(!Compile(draft), "wind absolute speed bound checked");
    draft.FromFormula(Formula::Dig(16));
    Require(static_cast<bool>(Compile(draft)), "maximum radius allowed");
    draft.Root().Values.Radius = std::numeric_limits<double>::infinity();
    Require(!Compile(draft), "nonfinite radius rejected");

    draft.FromFormula(Formula::Bolt().After(0));
    draft.Root().Children.clear();
    Require(!Compile(draft), "missing fixed child rejected without fallback empty");
    draft.FromFormula(Formula::Sequence({Formula{}, Formula{}}));
    draft.Root().Children[1]->Id = draft.Root().Children[0]->Id;
    Require(!Compile(draft), "duplicate editor identity rejected");
    draft.Root().Children[1].reset();
    Require(!Compile(draft), "null child rejected safely");
    draft.FromFormula(Formula::Bolt());
    auto &root = draft.Root();
    for (int i = 0; i < 16; ++i) {
        auto previous = std::make_unique<DraftNode>(std::move(root));
        root = DraftNode{static_cast<NodeId>(10000 + i), NodeKind::Delay, {}, {}};
        root.Children.push_back(std::move(previous));
    }
    Require(!Compile(draft), "directly corrupted tree still receives independent compile depth error");
}

void SharedAnalysisDiagnostics() {
    Draft draft;
    draft.FromFormula(Formula::Sequence({Formula{}, Formula::Bolt(1, 601, 1).After(1)}));
    const auto &bolt = *draft.Root().Children[1]->Children[0];
    const auto invalid = Compile(draft);
    Require(!invalid && invalid.Errors.front().Node == bolt.Id && invalid.Errors.front().Field == "speed",
            "shared workload diagnostic must map back to the original editor ID");
    draft.FromFormula(Formula::Bolt(std::numeric_limits<double>::max(), 0, 1).MultiplyDamage(2));
    const auto overflow = Compile(draft);
    Require(!overflow && overflow.Errors.front().Node == draft.Root().Id && overflow.Errors.front().Field == "value",
            "modifier arithmetic diagnostic must retain its editor identity");

    const auto shared = Formula::Bolt(1, 0, 601);
    draft.FromFormula(Formula::Sequence({shared, shared}));
    const auto repeated = Compile(draft);
    Require(repeated.Errors.size() == 2 && repeated.Errors[0].Node == draft.Root().Children[0]->Id
                && repeated.Errors[1].Node == draft.Root().Children[1]->Id,
            "shared formula occurrences must map to distinct editor nodes");

    const auto description = Formula::Bolt(10, 30, 3).OnHit(Formula::Wind(1, 2).After(2)).Repeated(3, 5);
    draft.FromFormula(description);
    const auto compiled = Compiled(draft);
    const auto analyzed = validation::Analyze(description);
    Require(analyzed && compiled.Budget.Nodes == analyzed.Budget.Nodes && compiled.Budget.Depth == analyzed.Budget.Depth
                && compiled.Budget.Effects == analyzed.Budget.Effects && compiled.Budget.WindEffects == analyzed.Budget.WindEffects
                && compiled.Budget.NodeVisits == analyzed.Budget.NodeVisits && compiled.Budget.EndTick == analyzed.Budget.EndTick,
            "editor compilation and direct runtime analysis must share the same workload rules");
}

void EditingGrowthBounds() {
    Draft draft;
    const auto deepest = draft.Root().Id;
    for (int i = 0; i < 15; ++i)
        Require(draft.Wrap(draft.Root().Id, NodeKind::Delay) != 0, "wrap accepts depth through 16");
    const auto deepIds = Ids(draft);
    const auto selected = draft.Root().Id;
    Require(draft.Wrap(selected, NodeKind::Delay) == 0 && draft.Root().Id == selected && Ids(draft) == deepIds,
            "sixteenth wrap rejects before mutation and preserves selection");
    Require(Compiled(draft).Budget.Depth == 16, "rejected wrap leaves valid compileable depth");
    Require(draft.Replace(deepest, NodeKind::OnHit) == 0 && Ids(draft) == deepIds,
            "replace rejects fixed-slot descendants beyond maximum depth");
    Require(draft.Replace(draft.Find(selected)->Children[0]->Id, NodeKind::OnHit) != 0,
            "replace may shrink a deep subtree to a valid fixed-slot node");
    Require(draft.Unwrap(selected) != 0 && static_cast<bool>(Compile(draft)), "unwrap remains available after depth rejection");

    draft.FromFormula(Formula::Sequence({}));
    const auto flatRoot = draft.Root().Id;
    for (int i = 0; i < 63; ++i)
        Require(draft.AddChild(flatRoot, NodeKind::Empty) != 0, "flat sequence accepts exactly 63 children");
    const auto flatIds = Ids(draft);
    Require(flatIds.size() == 64 && draft.AddChild(flatRoot, NodeKind::Empty) == 0 && Ids(draft) == flatIds,
            "flat AddChild enforces total node count including sequence root");

    draft.FromFormula(Formula::Bolt());
    for (int i = 0; i < 5; ++i)
        Require(draft.Duplicate(draft.Root().Id) != 0, "repeated root duplicate grows only within shape budget");
    const auto fullIds = Ids(draft);
    Require(fullIds.size() == 63, "fifth root duplicate has exactly 63 nodes");
    const auto fullRoot = draft.Root().Id;
    for (int i = 0; i < 10; ++i)
        Require(draft.Duplicate(fullRoot) == 0 && draft.Root().Id == fullRoot && Ids(draft) == fullIds,
                "repeated rejected duplicates retain bounded size, identities and selection");
    const auto lastChild = draft.AddChild(fullRoot, NodeKind::Empty);
    Require(lastChild == *fullIds.rbegin() + 1 && Ids(draft).size() == 64,
            "rejected duplicates allocate no IDs and next edit reaches exactly 64");
    const auto sixtyFour = Ids(draft);
    for (int i = 0; i < 10; ++i)
        Require(draft.AddChild(fullRoot, NodeKind::Bolt) == 0 && Ids(draft) == sixtyFour,
                "add child rejects the 65th node without allocating IDs or nodes");
    Require(draft.Replace(lastChild, NodeKind::OnHit) == 0 && Ids(draft) == sixtyFour,
            "replace rejects a larger fixed-slot structure when no room remains");
    Require(draft.Wrap(lastChild, NodeKind::OnHit) == 0 && Ids(draft) == sixtyFour,
            "wrap accounts for both wrapper and payload slots");
    const auto innerSequence = draft.Root().Children[0]->Id;
    Require(draft.AddChild(innerSequence, NodeKind::Bolt) == 0 && Ids(draft) == sixtyFour,
            "projected subtree growth includes already pending siblings in the node bound");
    Require(draft.Delete(lastChild) == fullRoot && Ids(draft).size() == 63, "delete releases an editor node after rejection");
    const auto next = draft.AddChild(fullRoot, NodeKind::Empty);
    Require(next == lastChild + 1, "deleted IDs are never reused");
    Require(draft.Delete(next) == fullRoot, "new node remains normally editable");
    const auto branch = draft.Root().Children.back()->Id;
    Require(draft.Delete(branch) == fullRoot, "delete can remove a duplicated subtree");
    Require(draft.Unwrap(fullRoot) != 0 && static_cast<bool>(Compile(draft)), "unwrap recovers normally after full-tree rejection");

    draft.FromFormula(Formula::Bolt());
    const auto previousIds = Ids(draft);
    const auto previousRoot = draft.Root().Id;
    Invalid([&] { draft.FromFormula(Formula::Sequence(std::vector<Formula>(64))); },
            "oversized formula must be rejected before import");
    auto tooDeep = Formula::Bolt();
    for (int i = 0; i < 16; ++i)
        tooDeep = tooDeep.After(0);
    Invalid([&] { draft.FromFormula(tooDeep); }, "excessively deep formula must be rejected before import");
    Require(draft.Root().Id == previousRoot && Ids(draft) == previousIds,
            "failed imports preserve previous tree and selection");
    const auto wrapper = draft.Wrap(previousRoot, NodeKind::Sequence);
    Require(wrapper == *previousIds.rbegin() + 1, "failed imports consume no node IDs");

    draft.Root().Children.push_back(std::make_unique<DraftNode>(DraftNode{50000, NodeKind::Sequence, {}, {}}));
    auto &invalidChildren = draft.Root().Children.back()->Children;
    for (NodeId i = 0; i < 65; ++i)
        invalidChildren.push_back(std::make_unique<DraftNode>(DraftNode{60000 + i, NodeKind::Empty, {}, {}}));
    Require(!Compile(draft), "direct oversized edits retain compile diagnostics");
    Require(draft.Replace(50000, NodeKind::Empty) == wrapper && static_cast<bool>(Compile(draft)),
            "replace can repair a directly corrupted oversized subtree");
}

void SimulatorDrainAndDiscard() {
    Simulator sim;
    Require(sim.IsIdle() && sim.PendingCount() == 0, "new simulator idle");
    sim.Cast(Formula::Bolt(1, 0, 3).OnHit(Formula::Dig(2)));
    Require(!sim.IsIdle() && sim.PendingCount() == 1, "queued root is pending even without a projectile");
    sim.AdvanceTo(0);
    auto first = sim.DrainTrace();
    Require(first.size() == 1 && sim.Trace().empty() && sim.DrainTrace().empty(), "trace drains exactly once");
    Require(sim.DiscardProjectile(1) && !sim.DiscardProjectile(1), "discard only consumes live carrier once");
    Require(sim.Projectiles().empty() && !sim.IsIdle() && sim.PendingCount() == 1, "stale expiration remains a real pending job");
    sim.AdvanceTo(3);
    Require(sim.IsIdle() && sim.DrainTrace().empty(), "discard never emits hit, expiry or payload");
    sim.Cast(Formula::Dig(1).After(4));
    sim.AdvanceTo(3);
    Require(!sim.IsIdle() && sim.Projectiles().empty(), "delayed empty projectile list is not idle");
    sim.AdvanceTo(7);
    Require(sim.IsIdle() && sim.DrainTrace().size() == 1, "delayed immediate effect drains normally");
}
} // namespace

int main() {
    try {
        for (const auto &[name, test] : std::vector<std::pair<const char *, std::function<void()>>>{
                 {"structure editing", StructuralEditing}, {"round trip and scope", RoundTripAndScope},
                 {"parameter and structure errors", ParameterAndStructureErrors},
                 {"shared analysis diagnostics", SharedAnalysisDiagnostics},
                 {"bounded editor growth", EditingGrowthBounds},
                 {"incremental trace and cancellation", SimulatorDrainAndDiscard}}) {
            test();
            std::cout << "PASS " << name << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
