#include "FormulaValidation/FormulaAnalysis.h"
#include "Formula/Simulator.h"
#include <iostream>
#include <limits>

using namespace pixel::formula;
using namespace pixel::formula::validation;
namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void BudgetBounds() {
    const auto chain = Formula::Bolt(1, 0, 2).OnHit(Formula::Bolt(1, 0, 3).OnHit(Formula::Wind(0, 0)));
    auto result = Analyze(chain.Repeated(4, 5).Spread(4, 10));
    Require(static_cast<bool>(result), "legal descendant budget accepted");
    Require(result.Budget.Effects == 48 && result.Budget.WindEffects == 16 && result.Budget.EndTick == 20,
            "effect, wind and time budget cover complete descendants");
    result = Analyze(chain.Repeated(4, 5).Spread(6, 10));
    Require(!result && result.Errors[0].Field == "effects", "payload descendants cannot escape effect bound");
    result = Analyze(Formula{}.Repeated(16, 0).Repeated(16, 0).Repeated(16, 0));
    Require(!result && result.Errors[0].Field == "nodeVisits", "empty expansion still consumes evaluator work");
    result = Analyze(Formula::Bolt().After(600).Repeated(7, 600));
    Require(!result && result.Errors[0].Field == "endTick", "delayed descendants must end within 3600 ticks");
    result = Analyze(Formula::Bolt().Repeated(0, 600));
    Require(result && result.Budget.Effects == 0 && result.Budget.EndTick == 0, "zero repeat remains empty without phantom lifetime");
    result = Analyze(Formula::Dig(1).OnHit(Formula::Wind(1, 2).Repeated(16, 0)));
    Require(result && result.Budget.Effects == 1 && result.Budget.WindEffects == 0, "noncarrier does not fire payloads");
    result = Analyze(Formula::Wind(0, 0).Repeated(16, 0).Repeated(4, 0));
    Require(result && result.Budget.Effects == 64 && result.Budget.WindEffects == 64,
            "zero-radius or zero-impulse wind still reserves full potential budget");
    const auto multiple = Formula::Bolt().OnHit(Formula::Bolt().Spread(4, 2)).OnHit(Formula::Wind(1, 2).Repeated(4, 0));
    result = Analyze(multiple.Spread(8, 2));
    Require(!result && result.Errors[0].Field == "effects", "multiple OnHit bindings each count for every carrier");
}

void ShapeAndParameterLimits() {
    Require(static_cast<bool>(Analyze(Formula::Sequence(std::vector<Formula>(63)))),
            "64-node tree must remain legal");
    const auto tooWide = Formula::Sequence(std::vector<Formula>(64));
    Require(Analyze(tooWide).Errors.front().Field == "nodes", "65-node tree must be rejected before evaluation");
    auto deep = Formula::Bolt();
    for (int i = 0; i < 15; ++i) deep = deep.After(0);
    Require(Analyze(deep).Budget.Depth == 16, "depth 16 must remain legal");
    Require(Analyze(deep.After(0)).Errors.front().Field == "depth", "depth 17 must be rejected before evaluation");

    for (const auto &[description, field] : std::vector<std::pair<Formula, const char *>>{
             {Formula::Bolt(1, 0, 601), "lifetime"}, {Formula::Bolt(1, 601, 1), "speed"},
             {Formula::Dig(17), "radius"}, {Formula::Wind(1, -601), "impulse"},
             {Formula{}.After(601), "ticks"}, {Formula{}.Repeated(17, 0), "count"},
             {Formula{}.Repeated(1, 601), "interval"}, {Formula{}.Spread(17, 0), "count"}}) {
        const auto result = Analyze(description);
        Require(!result && result.Errors.front().Field == field, "game-host parameter bound was lost");
    }
    // These values are legal language descriptions; host policy rejects them.
    Simulator standalone;
    standalone.Cast(Formula::Bolt(1, 601, 601));
    standalone.AdvanceTo(0);
    Require(standalone.Projectiles().size() == 1, "game limits leaked into standalone Formula");

    const auto result = Analyze(Formula::Sequence({Formula{}, Formula::Bolt(1, 601, 1).After(1)}));
    Require(!result && result.Errors.front().Path == std::vector<std::size_t>{1, 0},
            "diagnostic must identify the nested node occurrence");
    const auto overflow = Analyze(Formula::Bolt(std::numeric_limits<double>::max(), 0, 1).MultiplyDamage(2));
    Require(!overflow && overflow.Errors.front().Path.empty() && overflow.Errors.front().Field == "value",
            "overflow must locate its modifier rather than the bolt");
    const auto repeated = Formula::Bolt(1, 0, 601);
    const auto shared = Analyze(Formula::Sequence({repeated, repeated}));
    Require(shared.Errors.size() == 2 && shared.Errors[0].Path == std::vector<std::size_t>{0}
                && shared.Errors[1].Path == std::vector<std::size_t>{1},
            "shared descriptions must retain distinct diagnostic occurrences");
}
} // namespace

int main() {
    try {
        BudgetBounds();
        ShapeAndParameterLimits();
        std::cout << "Formula game-host validation without editor dependencies passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
