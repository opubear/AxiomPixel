#include "Scenarios.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace pixel::formula::experiment {
namespace {
constexpr Effect Dig{EffectKind::Dig, 1};
constexpr Effect Wind{EffectKind::Wind, 2, 4};
constexpr Effect Bolt{EffectKind::Bolt};
constexpr std::array Scenarios{Scenario::Dig, Scenario::Hit, Scenario::Periodic, Scenario::InventoryStop, Scenario::Wake};

void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

Formula LegacySource(Scenario scenario) {
    switch (scenario) {
    case Scenario::Dig: return Parse("cast dig(1);");
    case Scenario::Hit: return Parse("cast on_hit(bolt(10, 60, 20), dig(1));");
    case Scenario::Periodic: return Parse("cast repeat(6, 2, wind(2, 4));");
    case Scenario::InventoryStop: return Parse("cast dig(1);");
    case Scenario::Wake: return Parse("cast wind(2, 4);");
    }
    throw std::logic_error("unknown scenario");
}

Block HybridSource(Scenario scenario) {
    switch (scenario) {
    case Scenario::Dig: return {Emit(Dig)};
    case Scenario::Hit: return {Emit(Bolt), Await(EventKind::Hit), Emit(Dig)};
    case Scenario::Periodic: return {While(Constant(1), {Emit(Wind), Wait(2)})};
    case Scenario::InventoryStop:
        return {While(Less(Inventory(), Constant(3)), {Emit(Dig), Wait(1)})};
    case Scenario::Wake: return {Await(EventKind::Wake), Emit(Wind)};
    }
    throw std::logic_error("unknown scenario");
}

// The fake host's only material rule: each accepted dig adds one inventory item.
// This is a comparison fixture, not Pixel's mining/inventory implementation.
void Commit(Observation &observation, Integer &inventory, Tick tick, const Slice &slice) {
    Require(slice.Trap.empty(), "unexpected program trap");
    observation.Work += slice.Work;
    observation.Queries += slice.Queries;
    for (const auto &intent : slice.Effects) {
        observation.Trace.push_back({tick, intent.Value, intent.PositionX});
        if (intent.Value.Kind == EffectKind::Dig) ++inventory;
    }
}

void Drain(Simulator &simulator, Observation &observation, Integer &inventory) {
    for (const auto &entry : simulator.DrainTrace()) {
        const auto x = static_cast<Integer>(entry.Position.X);
        if (const auto *dig = std::get_if<DigArea>(&entry.Data)) {
            observation.Trace.push_back({entry.At, {EffectKind::Dig, static_cast<Integer>(dig->Radius)}, x});
            ++inventory;
        } else if (const auto *wind = std::get_if<WindImpulse>(&entry.Data)) {
            observation.Trace.push_back({entry.At, {EffectKind::Wind, static_cast<Integer>(wind->Radius),
                                                   static_cast<Integer>(wind->Velocity.X)}, x});
        } else if (std::holds_alternative<BoltSpawn>(entry.Data)) {
            observation.Trace.push_back({entry.At, Bolt, x});
        }
    }
    observation.PeakPending = std::max(observation.PeakPending, simulator.PendingCount());
}

std::vector<Record> Expected(Scenario scenario) {
    switch (scenario) {
    case Scenario::Dig: return {{0, Dig, 0}};
    case Scenario::Hit: return {{0, Bolt, 0}, {4, Dig, 7}};
    case Scenario::Periodic: return {{0, Wind, 0}, {2, Wind, 0}, {4, Wind, 0},
                                    {6, Wind, 0}, {8, Wind, 0}, {10, Wind, 0}};
    case Scenario::InventoryStop: return {{0, Dig, 0}, {1, Dig, 0}, {2, Dig, 0}};
    case Scenario::Wake: return {{4, Wind, 0}};
    }
    throw std::logic_error("unknown scenario");
}

void VerifySlices() {
    const Program snapshotProgram({Emit(Dig), Assign(0, Inventory()),
        If(Less(Local(0), Constant(1)), {Emit(Wind)}), Wait(1), Assign(1, Inventory())});
    State state;
    auto first = Step(snapshotProgram, state, {0, 0});
    Require(first.Effects.size() == 2 && state.Locals[0] == 0, "query must not read buffered effects");
    Require(first.Effects[0].Value == Dig && first.Effects[1].Value == Wind, "effects keep source order");
    auto second = Step(snapshotProgram, state, {1, 1});
    Require(second.Effects.empty() && state.Locals[1] == 1, "next slice must acquire the new snapshot");

    // An interpreter cannot observe another instance's effects in the same stage.
    const Program readAndDig({If(Less(Inventory(), Constant(1)), {Emit(Dig)})});
    State a, b;
    const View frozen{0, 0};
    Require(Step(readAndDig, a, frozen).Effects.size() == 1
        && Step(readAndDig, b, frozen).Effects.size() == 1, "stage view must be shared by value");

    const Program loop({Assign(0, Constant(0)), While(Less(Local(0), Constant(3)),
        {Emit(Wind), Assign(0, Add(Local(0), Constant(1))), Wait(2)})});
    State counted;
    std::vector<Tick> ticks;
    for (Tick tick = 0; tick < 8; ++tick) {
        if (!Step(loop, counted, {tick, 0}).Effects.empty()) ticks.push_back(tick);
    }
    Require(ticks == std::vector<Tick>{0, 2, 4} && counted.Status == Phase::Done,
            "local assignment and loop order");

    State beforeSave;
    Step(loop, beforeSave, {0, 0});
    auto afterLoad = beforeSave;
    Require(afterLoad.Locals[0] == 1, "checkpoint contains the mutated local");
    for (Tick tick = 1; tick < 8; ++tick) {
        const auto uninterrupted = Step(loop, beforeSave, {tick, 0});
        const auto restored = Step(loop, afterLoad, {tick + 1000, 0});
        Require(uninterrupted.Effects.size() == restored.Effects.size()
            && beforeSave.Locals == afterLoad.Locals && beforeSave.NextEmission == afterLoad.NextEmission,
            "checkpoint keeps loop position, locals and output sequence");
    }

    // A trap drops this slice, including effects before the failing operation.
    const Program runaway({Emit(Dig), While(Constant(1), {})});
    State trapped;
    auto bad = Step(runaway, trapped, {0, 0});
    Require(bad.Trap == "work limit" && bad.Effects.empty() && bad.Work == 64
        && bad.Location.Line != 0 && bad.Location.File.ends_with("Scenarios.cpp"), "bounded work and source location");
    Require(Step(runaway, trapped, {1, 0}).Effects.empty(), "trapped instances cannot restart");

    const Program partial({Emit(Dig), Wait(1), Emit(Wind), While(Constant(1), {})});
    State committed;
    Require(Step(partial, committed, {0, 0}).Effects.size() == 1, "first slice commits");
    auto failed = Step(partial, committed, {1, 1});
    Require(failed.Effects.empty() && !failed.Trap.empty(), "later trap must discard only its own slice");

    const Program queries({While(Constant(1), {Assign(0, Inventory())})});
    State queryState;
    Require(Step(queries, queryState, {0, 0}, {}, {64, 2, 16}).Trap == "query limit", "query budget");
    const Program effects({Emit(Dig), Emit(Wind)});
    State effectState;
    auto overflow = Step(effects, effectState, {0, 0}, {}, {64, 8, 1});
    Require(overflow.Trap == "effect limit" && overflow.Effects.empty(), "effect budget rolls back output");
    const Program arithmetic({Emit(Dig), Assign(0, Add(Constant(std::numeric_limits<Integer>::max()), Constant(1)))});
    State arithmeticState;
    auto arithmeticResult = Step(arithmetic, arithmeticState, {0, 0});
    Require(arithmeticResult.Trap == "integer overflow" && arithmeticResult.Effects.empty(), "checked arithmetic");

    // The compiler's implicit terminator also consumes work, but its diagnostic
    // must still point to authored code when preceding statements use it all.
    Block exactBudget(31, Assign(0, Constant(1)));
    exactBudget.push_back(Emit(Dig));
    exactBudget.push_back(Emit(Wind));
    const auto endSource = exactBudget.back().Source;
    const Program exactProgram(exactBudget);
    State exactState;
    const auto exhausted = Step(exactProgram, exactState, {0, 0});
    Require(exhausted.Work == 64 && exhausted.Trap == "work limit" && exhausted.Effects.empty()
        && exhausted.Location.File == endSource.File && exhausted.Location.Line == endSource.Line,
        "implicit stop must map to the last authored statement");
    const Program empty({});
    State emptyState;
    const auto emptyTrap = Step(empty, emptyState, {0, 0}, {}, {0, 8, 16});
    Require(emptyTrap.Trap == "work limit" && emptyTrap.Location.File.ends_with("Scenarios.cpp"),
        "empty program must map to its authoring site");
}

void VerifyEventsAndState() {
    const Program program({Emit(Bolt), Await(EventKind::Hit), Emit(Dig), Wait(2), Emit(Wind)});
    State state;
    auto emitted = Step(program, state, {0, 0});
    Require(emitted.Effects.size() == 1 && emitted.Effects[0].Token == 1, "correlation token");
    std::array events{HostEvent{1, 1, EventKind::Hit, 99, 3}, HostEvent{2, 1, EventKind::Hit, 1, 7}};
    Require(Step(program, state, {1, 0, 2}, events).Effects.empty(), "event cannot resume in its commit tick");
    auto resumed = Step(program, state, {2, 0, 2}, events);
    Require(resumed.Effects.size() == 1 && resumed.Effects[0].PositionX == 7, "match request and capture hit origin");

    // Value checkpoints demonstrate required state; they are not a file format.
    static_assert(std::is_trivially_copyable_v<State>);
    auto restored = state;
    Require(Step(program, state, {3, 0, 2}, events).Effects.empty(), "remaining delay");
    Require(Step(program, restored, {1003, 0, 2}, events).Effects.empty(), "frozen time must not catch up");
    const auto originalTail = Step(program, state, {4, 0, 2}, events);
    const auto restoredTail = Step(program, restored, {1004, 0, 2}, events);
    Require(originalTail.Effects.size() == 1 && restoredTail.Effects.size() == 1
        && restoredTail.Effects[0].Token == originalTail.Effects[0].Token
        && restoredTail.Effects[0].PositionX == 7, "resume without replaying prior effects");

    const Program wake({Await(EventKind::Wake), Emit(Wind), Await(EventKind::Wake), Emit(Dig)});
    State waiting;
    std::array wakeEvents{HostEvent{1, 0, EventKind::Wake, 0, 0}};
    Step(wake, waiting, {1, 0, 1}, wakeEvents);
    Require(Step(wake, waiting, {2, 0, 1}, wakeEvents).Effects.empty(), "await subscribes only to future events");
    std::array fresh{HostEvent{2, 2, EventKind::Wake, 0, 0}};
    auto listeningCopy = waiting;
    Require(Step(wake, waiting, {3, 0, 2}, fresh).Effects.size() == 1, "wake event resumes once");
    Require(Step(wake, listeningCopy, {3, 0, 2}, fresh).Effects.size() == 1, "listening checkpoint keeps filter");
    Require(Step(wake, waiting, {4, 0, 2}, fresh).Effects.empty(), "same event cannot satisfy a second await");

    const Program receipt({Emit(Dig), Await(EventKind::Rejected), Emit(Wind)});
    State rejected;
    Step(receipt, rejected, {0, 0});
    std::array rejection{HostEvent{1, 0, EventKind::Rejected, 1, 0}};
    Require(Step(receipt, rejected, {1, 0, 1}, rejection).Effects.size() == 1,
            "host rejection is an explicit event, not a fatal runtime bug");

    const Program oldProgram({Wait(1), Emit(Dig)}), newProgram({Emit(Wind)});
    State oldInstance, newInstance;
    Step(oldProgram, oldInstance, {0, 0});
    Require(Step(newProgram, newInstance, {1, 0}).Effects[0].Value.Kind == EffectKind::Wind
        && Step(oldProgram, oldInstance, {1, 0}).Effects[0].Value.Kind == EffectKind::Dig,
        "new artifact does not modify the old program");

    State corrupt;
    corrupt.PC = 999;
    bool propagated = false;
    try { Step(program, corrupt, {0, 0}); } catch (const std::out_of_range &) { propagated = true; }
    Require(propagated, "internal invalid PC must propagate");
    bool zeroWaitRejected = false;
    try { Program invalid({Wait(0)}); } catch (const std::invalid_argument &) { zeroWaitRejected = true; }
    Require(zeroWaitRejected, "zero wait must not silently spin");
}

void VerifyLongPeriod() {
    Simulator legacy;
    legacy.Cast(Formula::Wind(2, 4).Repeated(500, 2));
    legacy.AdvanceTo(0);
    Require(legacy.PendingCount() == 499, "legacy repeat expands its finite future work");
    const auto program = Program(HybridSource(Scenario::Periodic));
    State state;
    std::size_t emitted = 0;
    for (Tick tick = 0; tick < 1000; ++tick) {
        const auto slice = Step(program, state, {tick, 0});
        Require(slice.Trap.empty() && state.Status == Phase::Sleeping, "periodic process keeps one continuation");
        emitted += slice.Effects.size();
    }
    Require(emitted == 500 && state.Remaining == 1, "periodic process continues beyond the comparison window");
    legacy.AdvanceTo(999);
    Require(legacy.Trace().size() == emitted && legacy.IsIdle(), "long finite prefix agrees");
}
} // namespace

const char *Name(Scenario scenario) {
    switch (scenario) {
    case Scenario::Dig: return "dig";
    case Scenario::Hit: return "hit";
    case Scenario::Periodic: return "periodic";
    case Scenario::InventoryStop: return "inventory_stop";
    case Scenario::Wake: return "wake";
    }
    throw std::logic_error("unknown scenario");
}
Fixture::Fixture(Scenario scenario) : Kind(scenario), Legacy(LegacySource(scenario)), Hybrid(HybridSource(scenario)) {}

Observation RunLegacy(const Fixture &fixture) {
    Observation result;
    Simulator simulator;
    Integer inventory = 0;
    bool stopped = false;
    if (fixture.Kind != Scenario::InventoryStop && fixture.Kind != Scenario::Wake) simulator.Cast(fixture.Legacy);
    for (Tick tick = 0; tick < 12; ++tick) {
        simulator.AdvanceTo(tick);
        // These two decisions are necessarily in C++, not the existing DSL.
        if (fixture.Kind == Scenario::InventoryStop && !stopped) {
            ++result.HostDecisions;
            ++result.Queries;
            if (inventory < 3) simulator.Cast(fixture.Legacy);
            else stopped = true;
        }
        if (fixture.Kind == Scenario::Wake && !stopped) {
            ++result.HostDecisions;
            if (tick == 4) { simulator.Cast(fixture.Legacy); stopped = true; }
        }
        simulator.AdvanceTo(tick);
        Drain(simulator, result, inventory);
        if (fixture.Kind == Scenario::Hit && tick == 3)
            Require(simulator.Hit(1, {7, 0}), "legacy fixture hit");
        result.PeakPending = std::max(result.PeakPending, simulator.PendingCount());
    }
    return result;
}

Observation RunHybrid(const Fixture &fixture) {
    Observation result;
    State state;
    Integer inventory = 0;
    std::vector<HostEvent> events;
    for (Tick tick = 0; tick < 12; ++tick) {
        const View view{tick, inventory, events.empty() ? 0 : events.back().Sequence};
        auto slice = Step(fixture.Hybrid, state, view, events);
        Commit(result, inventory, tick, slice);
        result.PeakPending = std::max(result.PeakPending,
            std::size_t(state.Status == Phase::Sleeping || state.Status == Phase::Listening));
        if (tick == 3 && (fixture.Kind == Scenario::Hit || fixture.Kind == Scenario::Wake))
            events.push_back({1, tick, fixture.Kind == Scenario::Hit ? EventKind::Hit : EventKind::Wake,
                              fixture.Kind == Scenario::Hit ? 1u : 0u, 7});
    }
    return result;
}

void Verify() {
    for (const auto scenario : Scenarios) {
        const Fixture fixture(scenario);
        const auto legacy = RunLegacy(fixture), hybrid = RunHybrid(fixture);
        Require(legacy.Trace == Expected(scenario), "legacy trace differs from explicit expected behavior");
        Require(hybrid.Trace == Expected(scenario), "hybrid trace differs from explicit expected behavior");
        std::cout << Name(scenario) << ": matched " << hybrid.Trace.size() << " effects\n";
    }
    VerifySlices();
    VerifyEventsAndState();
    VerifyLongPeriod();
    for (const auto source : {"cast inventory();", "cast await();", "cast while(1, dig(1));"}) {
        bool rejected = false;
        try { Parse(source); } catch (const ParseError &error) { rejected = error.Location.Line == 1; }
        Require(rejected, "baseline must explicitly reject unsupported language constructs");
    }
    std::cout << "snapshot, local state, ordering, budgets, events, freeze and resume contracts passed\n";
}

void Benchmark() {
    using Clock = std::chrono::steady_clock;
    std::cout << "scenario,variant,samples,p50_us,p95_us,effects,peak_pending,work,queries,host_decisions,state_bytes\n";
    for (const auto scenario : Scenarios) {
        const Fixture fixture(scenario); // Compilation is deliberately outside timing.
        for (const bool hybrid : {false, true}) {
            std::vector<double> samples;
            Observation last;
            for (unsigned sample = 0; sample < 1100; ++sample) {
                const auto start = Clock::now();
                auto observed = hybrid ? RunHybrid(fixture) : RunLegacy(fixture);
                const auto end = Clock::now();
                Require(observed.Trace == Expected(scenario), "benchmark output mismatch");
                if (sample >= 100) samples.push_back(std::chrono::duration<double, std::micro>(end - start).count());
                last = std::move(observed);
            }
            std::sort(samples.begin(), samples.end());
            std::cout << Name(scenario) << ',' << (hybrid ? "hybrid" : "legacy") << ',' << samples.size()
                      << ',' << samples[499] << ',' << samples[949] << ',' << last.Trace.size()
                      << ',' << last.PeakPending << ',';
            if (hybrid) std::cout << last.Work;
            else std::cout << "unavailable";
            std::cout << ',' << last.Queries << ',' << last.HostDecisions << ',';
            if (hybrid) std::cout << sizeof(State);
            else std::cout << "unavailable";
            std::cout << '\n';
        }
    }
}
} // namespace pixel::formula::experiment
