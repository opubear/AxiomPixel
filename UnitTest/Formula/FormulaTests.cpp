#include "Formula/Simulator.h"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>

using namespace pixel::formula;
namespace {
void Require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void Near(double actual, double expected, const char *message) {
    Require(std::isfinite(actual) && std::abs(actual - expected) < 1e-9, message);
}
template <class E, class F>
void Throws(F &&call, const char *message) {
    try {
        call();
    } catch (const E &) {
        return;
    }
    throw std::runtime_error(message);
}
template <class T>
std::vector<TraceEntry> Entries(const Simulator &sim) {
    std::vector<TraceEntry> entries;
    for (const auto &entry : sim.Trace())
        if (std::holds_alternative<T>(entry.Data))
            entries.push_back(entry);
    return entries;
}
std::vector<std::string> Run(std::string_view program, Tick end = 10, bool partition = false) {
    Simulator sim;
    sim.Cast(Parse(program));
    if (partition)
        for (Tick tick = 0; tick <= end; ++tick)
            sim.AdvanceTo(tick);
    else
        sim.AdvanceTo(end);
    std::vector<std::string> result;
    for (const auto &event : sim.Trace())
        result.push_back(FormatTrace(event));
    return result;
}
void OrderAndImmutability() {
    Simulator sim;
    const auto root = Parse(R"(
        // One binding reused without accumulating mutations.
        let base = bolt();
        cast seq(base, base |> damage_add(5) |> damage_mul(2),
                       base |> damage_mul(2) |> damage_add(5), base);
    )");
    sim.Cast(root);
    sim.AdvanceTo(0);
    const auto bolts = Entries<BoltSpawn>(sim);
    Require(bolts.size() == 4, "four branches must emit");
    for (std::size_t i = 0; i < bolts.size(); ++i)
        Near(std::get<BoltSpawn>(bolts[i].Data).Damage, std::vector<double>{10, 30, 25, 10}[i], "pipeline order and shared base");
    Require(Format(Parse("cast " + Format(root) + ";")) == Format(root), "canonical text round-trip");
    Require(Run("cast " + Format(root) + ";") == Run(R"(
        cast seq(bolt(), damage_mul(2, damage_add(5, bolt())), damage_add(5, damage_mul(2, bolt())), bolt());
    )"),
            "pipeline lowers to ordinary composition");
    const auto cpp = Formula::Bolt().AddDamage(5).MultiplyDamage(2);
    Require(Format(cpp) == "damage_mul(2, damage_add(5, bolt(10, 60, 120)))", "C++ and language use the same description domain");
}
void ClosureAndScope() {
    Simulator sim;
    sim.Cast(Parse("cast seq(empty(), bolt(), wind(8, 120), dig(2)) |> damage_add(5) |> speed_mul(2);"));
    sim.AdvanceTo(0);
    const auto bolts = Entries<BoltSpawn>(sim);
    Require(bolts.size() == 1, "mixed composition must retain the bolt");
    Near(std::get<BoltSpawn>(bolts[0].Data).Damage, 15, "mixed group damage");
    Near(std::get<BoltSpawn>(bolts[0].Data).Velocity.X, 120, "mixed group speed");
    Near(std::get<WindImpulse>(Entries<WindImpulse>(sim)[0].Data).Velocity.X, 120, "unsupported speed modifier must preserve wind");
    Require(Entries<Diagnostic>(sim).size() == 4, "inapplicable modifiers must be visible");
    Require(Entries<DigArea>(sim).size() == 1, "non-projectile descriptions remain executable");
    Require(Run("cast seq(empty(), seq(bolt(), dig(1)), wind(2, 3));")
                == Run("cast seq(bolt(), seq(dig(1), wind(2, 3)), empty());"),
            "seq must be associative with empty identity");
    Require(Run("cast seq(repeat(0, 1, bolt()), fan(0, 10, bolt()), empty() |> damage_mul(2));").empty(), "zero copies and modified empty are empty");
    Require(Run("cast on_hit(dig(1), bolt());").size() == 2, "unsupported carrier diagnoses and preserves effect");
}
void SchedulingAndFan() {
    constexpr auto program = "cast bolt(6, 60, 2) |> fan(3, 10) |> repeat(3, 12) |> delay(6);";
    Simulator sim;
    sim.Cast(Parse(program), {10, 20});
    sim.AdvanceTo(5);
    Require(sim.Trace().empty(), "delay emits nothing early");
    sim.AdvanceTo(30);
    const auto bolts = Entries<BoltSpawn>(sim);
    Require(bolts.size() == 9, "repeat of fan must produce nine bolts");
    for (std::size_t i = 0; i < bolts.size(); ++i) {
        Require(bolts[i].At == 6 + (i / 3) * 12, "repeat uses activation-relative fixed ticks");
        Near(bolts[i].Position.X, 10, "delayed origin is captured");
    }
    Near(std::get<BoltSpawn>(bolts[0].Data).Velocity.Y, -10.418890660015819, "first fan angle -10 degrees");
    Near(std::get<BoltSpawn>(bolts[1].Data).Velocity.Y, 0, "center fan angle");
    Near(std::get<BoltSpawn>(bolts[2].Data).Velocity.Y, 10.418890660015819, "last fan angle +10 degrees");
    Require(Run(program, 50) == Run(program, 50, true), "clock partition must not change trace");
    Require(Run("cast seq(delay(0, bolt(1, 1, 1)), repeat(2, 0, bolt(2, 1, 1)));", 1)
                == Run("cast seq(bolt(1, 1, 1), bolt(2, 1, 1), bolt(2, 1, 1));", 1),
            "zero-time wrappers preserve order");
}
void TriggerIsolation() {
    Simulator sim;
    const auto root = Parse("cast on_hit(bolt(), seq(bolt(4, 30, 12), delay(2, dig(3)))) |> damage_mul(3) |> speed_mul(2) |> fan(2, 20);");
    sim.Cast(root, {5, 7}, 90);
    sim.AdvanceTo(6);
    const auto carriers = sim.Projectiles();
    Require(carriers.size() == 2, "on_hit applies to every fan carrier");
    Near(carriers[0].Damage, 30, "outer modifier applies to carrier");
    Require(sim.Hit(carriers[0].Id, {42, 24}), "first hit must be accepted");
    Require(!sim.Hit(carriers[0].Id, {0, 0}), "duplicate hit must not trigger twice");
    Require(!sim.Hit(999, {0, 0}), "unknown hit must not create effects");
    Require(Entries<BoltSpawn>(sim).size() == 2, "payload waits until next tick");
    sim.AdvanceTo(7);
    const auto bolts = Entries<BoltSpawn>(sim);
    Require(bolts.size() == 3, "exactly one payload bolt");
    Near(std::get<BoltSpawn>(bolts.back().Data).Damage, 4, "payload damage does not inherit outer modifier");
    const auto velocity = std::get<BoltSpawn>(bolts.back().Data).Velocity;
    Near(std::hypot(velocity.X, velocity.Y), 30, "payload speed independent");
    Near(velocity.Y, 29.544232590366242, "payload inherits carrier direction (80 degrees)");
    Near(bolts.back().Position.X, 42, "payload starts at supplied collision position");
    Require(bolts.back().Cast == carriers[0].Cast, "payload retains root cast identity");
    sim.AdvanceTo(9);
    const auto digs = Entries<DigArea>(sim);
    Require(digs.size() == 1 && digs[0].At == 9, "payload delay relative to event activation");
    sim.AdvanceTo(120);
    Require(Entries<ProjectileExpired>(sim).size() == 2, "hit carrier must not emit a later expiration");
}
void NestedTriggers() {
    Simulator sim;
    sim.Cast(Parse("cast on_hit(on_hit(bolt(), dig(1)), on_hit(bolt(2, 3, 4), wind(2, 5)));"));
    sim.AdvanceTo(0);
    sim.Hit(1, {1, 2});
    sim.AdvanceTo(1);
    Require(Entries<DigArea>(sim).size() == 1 && Entries<BoltSpawn>(sim).size() == 2, "stacked triggers both execute");
    Require(std::holds_alternative<DigArea>(sim.Trace()[2].Data), "stacked decorators execute inner to outer");
    Require(sim.Hit(2, {3, 4}), "payload can itself carry an event program");
    sim.AdvanceTo(2);
    Require(Entries<WindImpulse>(sim).size() == 1, "nested payload event executes once");
    Near(Entries<WindImpulse>(sim)[0].Position.X, 3, "nested collision updates origin");
}
void LifetimeAndIndependentCasts() {
    const auto formula = Parse("cast on_hit(bolt(1, 60, 2), dig(2));");
    Simulator a, b;
    a.Cast(formula);
    b.Cast(formula, {100, 0});
    a.AdvanceTo(1);
    b.AdvanceTo(1);
    Near(a.Projectiles()[0].Position.X, 1, "60 cells/s means one cell/tick");
    Near(b.Projectiles()[0].Position.X, 101, "shared AST has no shared runtime state");
    a.AdvanceTo(2);
    Require(!a.Hit(1, {}), "projectile is dead exactly at lifetime boundary");
    Require(Entries<DigArea>(a).empty(), "expiration is not a hit");
    Require(b.Hit(1, {}), "other simulator remains independent");
    b.AdvanceTo(2);
    Require(Entries<DigArea>(b).size() == 1, "hit before expiry triggers even when payload starts on expiry tick");
    Throws<std::invalid_argument>([&] { a.AdvanceTo(1); }, "clock cannot run backwards");
    a.Cast(Formula::Bolt(7, 0, 1));
    a.AdvanceTo(2);
    Require(a.Projectiles().size() == 1 && a.Projectiles()[0].Cast == 2, "new cast starts at current tick");
}
void Diagnostics() {
    for (const auto *source : {"", "cast missing;", "let a = a; cast a;", "let a = bolt(); let a = dig(1); cast a;",
                               "cast bolt(1);", "cast bolt()", "cast bolt(); cast bolt();", "cast unknown();", "cast seq(2);",
                               "cast bolt() |> bolt();", "cast delay(0.5, bolt());", "cast repeat(-1, 0, bolt());",
                               "cast speed_mul(-2, bolt());", "cast bolt(1, 2, 0);", "cast wind(-1, 2);", "cast bolt(1e999, 2, 3);",
                               "cast bolt() | damage_add(2);", "cast dig(1e);", "cast delay(18446744073709551616, bolt());"})
        Throws<ParseError>([&] { Parse(source); }, "invalid program must report ParseError");
    try {
        Parse("// first line\ncast missing;");
    } catch (const ParseError &error) {
        Require(error.Location.Line == 2 && error.Location.Column == 6, "diagnostic must identify source location");
        Require(std::string(error.what()).find("unknown binding") != std::string::npos, "diagnostic reason");
        return;
    }
    throw std::runtime_error("missing diagnostic");
}
void NumericContracts() {
    for (double value : {std::numeric_limits<double>::denorm_min(), 1e-309,
                         std::numeric_limits<double>::min(), std::numeric_limits<double>::max()}) {
        const auto text = Format(Formula::Bolt(value, 0, 1));
        Require(Format(Parse("cast " + text + ";")) == text, "all finite doubles, including subnormals, round-trip");
    }
    Simulator sim;
    sim.Cast(Parse("cast seq(bolt() |> damage_add(-20), bolt(+1.5e1, .5, 1), repeat(1, 18446744073709551615, empty()));"));
    sim.AdvanceTo(0);
    Near(std::get<BoltSpawn>(Entries<BoltSpawn>(sim)[0].Data).Damage, 0, "negative damage addition clamps at zero");
    Near(std::get<BoltSpawn>(Entries<BoltSpawn>(sim)[1].Data).Damage, 15, "decimal and exponent syntax");
    const auto largeDelay = Formula{}.After(std::numeric_limits<Tick>::max());
    Require(Format(Parse("cast " + Format(largeDelay) + ";")) == Format(largeDelay), "64-bit ticks round-trip without double rounding");
    Throws<std::invalid_argument>([] { Formula::Bolt(-1); }, "native API checks semantic values");
    Throws<std::invalid_argument>([] { Formula::Wind(1, INFINITY); }, "non-finite descriptions rejected");
    Throws<std::invalid_argument>([&] { sim.Cast(Formula{}, {NAN, 0}); }, "invalid cast origin rejected");
    Simulator overflow;
    overflow.Cast(Formula::Bolt().After(std::numeric_limits<Tick>::max()));
    Throws<std::overflow_error>([&] { overflow.AdvanceTo(std::numeric_limits<Tick>::max()); }, "clock addition must not wrap");
    Simulator arithmetic;
    arithmetic.Cast(Formula::Bolt().MultiplyDamage(1e308));
    Throws<std::overflow_error>([&] { arithmetic.AdvanceTo(0); }, "modifier arithmetic must not produce infinity");
    Simulator expired;
    const auto extreme = Formula::Bolt(1, std::numeric_limits<double>::max(), 120).OnHit(Formula::Dig(1));
    expired.Cast(Formula::Sequence({extreme, Formula::Bolt(1, 0, 120).OnHit(Formula::Dig(2))}));
    Throws<std::overflow_error>([&] { expired.AdvanceTo(120); }, "extreme trajectory reports overflow");
    Require(expired.Projectiles().empty(), "failed expiry and unprocessed peers are no longer live");
    Require(!expired.Hit(1, {}) && !expired.Hit(2, {}), "expired carriers cannot trigger after an exception");
    expired.AdvanceTo(121);
    Require(Entries<DigArea>(expired).empty(), "failed final-position logging must not revive a carrier");
}
} // namespace
int main() {
    try {
        for (const auto &[name, test] : std::vector<std::pair<const char *, std::function<void()>>>{
                 {"order and immutability", OrderAndImmutability}, {"closure and scope", ClosureAndScope}, {"scheduling and fan", SchedulingAndFan}, {"trigger isolation", TriggerIsolation}, {"nested triggers", NestedTriggers}, {"lifetime and independent casts", LifetimeAndIndependentCasts}, {"source diagnostics", Diagnostics}, {"numeric contracts", NumericContracts}}) {
            test();
            std::cout << "PASS " << name << '\n';
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
