#include "ProcessFakeHost.h"
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

using namespace pixel::formula::process;
using pixel::formula::process::testing::FakeHost;
namespace {
void Require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template <class Exception, class Function>
void Throws(Function function, const char *message) {
    try { function(); }
    catch (const Exception &) { return; }
    throw std::runtime_error(message);
}
ProgramArtifact Program(std::string_view source, std::string_view resource = "test.process") {
    auto result = Compile(resource, source);
    if (const auto *error = std::get_if<Diagnostic>(&result))
        throw std::runtime_error(error->Message + " at " + std::to_string(error->Source.Line) + ':' + std::to_string(error->Source.Column));
    return std::get<ProgramArtifact>(std::move(result));
}
std::unique_ptr<Instance> Start(ProgramArtifact program, ExecutionLimits limits = {}, InstanceId id = 1, Position origin = {}) {
    auto result = Instance::Start(std::move(program), id, AllCapabilities, limits, origin);
    Require(std::holds_alternative<std::unique_ptr<Instance>>(result), "instance admission failed");
    return std::get<std::unique_ptr<Instance>>(std::move(result));
}
std::string Example(std::string_view name) {
    std::ifstream input(std::string(PROCESS_EXAMPLE_DIR) + '/' + std::string(name) + ".formula");
    Require(static_cast<bool>(input), "example file missing");
    return std::string(std::istreambuf_iterator<char>(input), {});
}
void Advance(FakeHost &host, int count) {
    for (int i = 0; i < count; ++i) Require(!host.Advance().Fault, "fake host run faulted");
}
HostEvent Event(std::uint64_t sequence, Boundary committed, RequestId request, Outcome outcome, Position hit = {}) {
    return {sequence, committed, request.Instance, outcome, request, {}, hit};
}
void GoldenScenarios() {
    FakeHost dig(Start(Program(Example("dig"))));
    Advance(dig, 2);
    Require(dig.Trace().size() == 1 && dig.Trace()[0].At == 0 && dig.Inventory == 1, "dig golden trace");
    Require(dig.CleanupCount() == 1 && dig.PendingCount() == 0, "completed host cleanup is exactly once");

    FakeHost hit(Start(Program(Example("hit"))));
    Advance(hit, 3);
    hit.Hit({7, 0, 0, 0}); // committed at boundary 3, visible at 4
    Advance(hit, 2);
    Require(hit.Trace().size() == 2 && hit.Trace()[1].At == 4 && hit.Trace()[1].Request.Origin.CellX == 7, "hit golden trace");

    FakeHost pulse(Start(Program(Example("pulse"))));
    Advance(pulse, 1000);
    Require(pulse.Trace().size() == 500, "bounded continuation supports long-running pulses");
    for (std::size_t i = 0; i < pulse.Trace().size(); ++i) Require(pulse.Trace()[i].At == 2 * i, "pulse interval");
    Require(pulse.PendingCount() <= 1 && pulse.OwnedCount() == 0, "pulse does not prequeue future work");
    pulse.Cancel();
    Require(pulse.PendingCount() == 0 && pulse.CleanupCount() == 1, "cancel drops queued replies");

    FakeHost inventory(Start(Program(Example("inventory"))));
    Advance(inventory, 6);
    Require(inventory.Trace().size() == 3 && inventory.Inventory == 3, "inventory loop terminates at frozen threshold");
    for (std::size_t i = 0; i < 3; ++i) Require(inventory.Trace()[i].At == i, "inventory golden tick");

    FakeHost wake(Start(Program(Example("wake"))));
    Advance(wake, 3);
    for (int repeat = 0; repeat < 256; ++repeat) wake.Wake();
    Require(wake.PendingCount() == 1, "wake subscription produces one terminal notification");
    Advance(wake, 2);
    Require(wake.Trace().size() == 1 && wake.Trace()[0].At == 4, "wake golden trace");
}
void IdentityAndPublication() {
    std::string source = "process 1; wait ticks(1); emit dig(cells(1));";
    const auto original = Program(source, "hot-reload");
    const auto copy = Program(source, "hot-reload");
    Require(original.Identity() == copy.Identity(), "content identity is deterministic");
    source = "process 1; emit dig(cells(9));";
    const auto replacement = Program(source, "hot-reload");
    Require(original.Identity().ContentSha256 != replacement.Identity().ContentSha256, "source replacement has new identity");
    Require(original.Source() != source && original.Identity().SemanticsVersion == 1 && original.Identity().HostContractVersion == 1,
            "artifact owns source and semantic versions");
    auto oldInstance = Start(original);
    oldInstance->Step({0, 0});
    auto newInstance = Start(replacement, {}, 2);
    const auto fresh = newInstance->Step({1, 0});
    const auto old = oldInstance->Step({1, 0});
    Require(std::get<Dig>(fresh.Requests[0].Description).Radius == 9 && std::get<Dig>(old.Requests[0].Description).Radius == 1,
            "published replacements cannot change old continuations");
    Require(oldInstance->Program().Identity() == original.Identity(), "instance remains bound to its artifact");
}
void DiagnosticsAndBounds() {
    const auto bad = Compile("bad-source", "process 1;\nlet x = true;\nemit x;");
    const auto &diagnostic = std::get<Diagnostic>(bad);
    Require(diagnostic.Resource == "bad-source" && diagnostic.Source.Line == 3 && diagnostic.Source.Column == 1,
            "compile diagnostic points into user text");
    constexpr std::array invalid{
        "cast dig(1);", "process 2;", "process 1; if 1 {}", "process 1; let x = true + false;",
        "process 1; let x = 1; x = 2;", "process 1; var x = 1; x = false;", "process 1; emit dig(1);",
        "process 1; wait ticks(true);", "process 1; wait ticks(1.0);", "process 1; if true {} else",
        "process 1; let x = 1; if true { let x = 2; }", "process 1; if true { let x = 1; } var y = x;",
        "process 1; fn recurse() { recurse(); }", "process 1; let x = recurse();", "process 1; await hit;",
        "process 1; var x = 1 == true;", "process 1; let x = 1 < 2.0;", "process 1; let x = 9223372036854775808;"};
    for (const auto source : invalid) Require(std::holds_alternative<Diagnostic>(Compile("invalid", source)), "invalid program accepted");
    const auto unavailable = Compile("denied", "process 1; emit dig(cells(1));", Mask(Capability::Wind));
    Require(std::holds_alternative<Diagnostic>(unavailable), "compile capability restriction");
    const auto program = Program("process 1; emit dig(cells(1));");
    Require(std::holds_alternative<Diagnostic>(Instance::Start(program, 1, 0)), "activation capability refusal");
    Throws<std::invalid_argument>([&] { Instance::Start(program, 0); }, "zero host instance ID must fail loudly");
    Throws<std::invalid_argument>([&] { Start(program, {0, 0, 0}); }, "invalid limits must fail loudly");
    Throws<std::invalid_argument>([&] { Start(program, {}, 1, {0, 0, 1, 0}); }, "noncanonical origin must fail loudly");

    const auto checkLimit = [](const std::string &source) {
        Require(std::holds_alternative<Diagnostic>(Compile("limit", source)), "compiler bound missing");
    };
    checkLimit("process 1;" + std::string(16384, ' '));
    checkLimit("process 1; let " + std::string(65, 'a') + " = 1;");
    checkLimit("process 1; var x = " + std::string(50, '(') + "1" + std::string(50, ')') + ';');
    std::string blocks = "process 1;", locals = "process 1;", instructions = "process 1;", expression = "process 1; let x = 1";
    for (int i = 0; i < 30; ++i) { blocks += "if true {"; expression += "+1"; }
    blocks += std::string(30, '}');
    for (int i = 0; i < 17; ++i) locals += "let x" + std::to_string(i) + " = 1;";
    for (int i = 0; i < 512; ++i) instructions += "stop;";
    checkLimit(blocks); checkLimit(locals); checkLimit(instructions); checkLimit(expression + ';');
}
void ValuesAndControl() {
    auto instance = Start(Program(R"(process 1;
        let base = bolt(10, speed(60), ticks(20));
        emit damage_mul(damage_add(base, 5), 2);
        emit damage_add(damage_mul(base, 2), 5);
        emit base;
        emit speed_mul(base, -2);
        var n = 0;
        while n < 3 { n = n + 1; }
        if !(n == 3) { emit dig(cells(99)); } else { emit dig(cells(2)); }
    )"));
    auto result = instance->Step({0, 0});
    Require(!result.Fault && result.Count == 5 && result.End, "pure values and control flow");
    Require(std::get<Bolt>(result.Requests[0].Description).Damage == 30, "modifier order add then multiply");
    Require(std::get<Bolt>(result.Requests[1].Description).Damage == 25, "modifier order multiply then add");
    Require(std::get<Bolt>(result.Requests[2].Description).Damage == 10, "immutable shared base");
    Require(std::get<Bolt>(result.Requests[3].Description).VelocityX == -120, "signed speed modifier");
    Require(std::get<Dig>(result.Requests[4].Description).Radius == 2, "mutable typed local loop");

    auto payload = Start(Program(R"(process 1;
        emit damage_mul(bolt(10, speed(60), ticks(20)), 3);
        await hit within ticks(5);
        if outcome() == hit { emit bolt(4, speed(30), ticks(12)); wait ticks(1); }
    )"));
    result = payload->Step({0, 0});
    const auto event = Event(1, 0, result.Requests[0].Id, Outcome::Hit, {9007199254740993LL, -9, .25, .5});
    result = payload->Step({1, 0}, {&event, 1});
    Require(result.Count == 1 && std::get<Bolt>(result.Requests[0].Description).Damage == 4,
            "carrier modifiers do not leak into hit payload");
    Require(result.Requests[0].Origin == event.HitPosition, "hit position preserves integer and subcell precision");
}
void BudgetTransactions() {
    auto exact = Start(Program("process 1; emit dig(cells(1));"), {5, 0, 1});
    const auto exactResult = exact->Step({0, 0});
    Require(exactResult.Count == 1 && exactResult.End && !exactResult.Fault && exactResult.WorkUsed == 5,
            "exact work and output budgets include implicit completion");
    auto noCompletionBudget = Start(Program("process 1; emit dig(cells(1));"), {4, 0, 1});
    const auto completionFault = noCompletionBudget->Step({0, 0});
    Require(completionFault.Count == 0 && completionFault.Fault && completionFault.Fault->Source.Offset == noCompletionBudget->Program().Source().size(),
            "implicit completion fault maps to actual end of source");

    auto work = Start(Program("process 1;\nemit dig(cells(1));\nwhile true {}"), {12, 8, 16});
    auto result = work->Step({0, 0});
    Require(result.Fault && result.Count == 0 && result.WorkUsed == 12 && result.End->Reason == EndReason::Faulted,
            "infinite loop traps and discards uncommitted effects");
    Require(result.Fault->Source.Line == 3, "work trap has source location");
    Require(!work->Step({1, 0}).End && !work->Cancel().End, "terminal cleanup emitted exactly once");

    auto outputs = Start(Program("process 1; while true { emit dig(cells(1)); }"), {4096, 8, 2});
    result = outputs->Step({0, 0});
    Require(result.Fault && result.Count == 0 && result.Fault->Message == "output budget exceeded", "output limit discards whole slice");
    auto fullOutput = Start(Program("process 1; var n = 0; while n < 64 { emit dig(cells(1)); n = n + 1; }"), {4096, 0, 64});
    const auto fullResult = fullOutput->Step({0, 0});
    Require(fullResult.Count == 64 && fullResult.End && !fullResult.Fault, "full fixed output buffer is usable");
    auto queries = Start(Program("process 1; emit dig(cells(1)); var x = inventory() + inventory();"), {256, 1, 16});
    result = queries->Step({0, 0});
    Require(result.Fault && result.Count == 0 && result.QueriesUsed == 1, "query budget discards whole slice");
    auto missing = Start(Program("process 1; let x = inventory();"));
    Require(missing->Step({0, {}}).Fault.has_value(), "missing frozen query is explicit");
    constexpr std::array faulty{
        "process 1; emit dig(cells(-1));", "process 1; wait ticks(0);", "process 1; await hit within ticks(1);",
        "process 1; let x = 9223372036854775807 + 1;", "process 1; let x = -(-9223372036854775807 + -1);",
        "process 1; emit damage_add(dig(cells(1)), 1);", "process 1; emit bolt(-1, speed(1), ticks(1));"};
    for (const auto source : faulty) Require(Start(Program(source))->Step({0, 0}).Fault.has_value(), "bad dynamic operation did not trap");

    FakeHost retained(Start(Program(R"(process 1;
        emit dig(cells(1)); emit bolt(1, speed(1), ticks(100)); wait ticks(1);
        emit dig(cells(2)); while true {}
    )"), {64, 8, 16}));
    Require(!retained.Advance().Fault && retained.Inventory == 1 && retained.OwnedCount() == 1, "first slice committed");
    result = retained.Advance();
    Require(result.Fault && retained.Trace().size() == 2 && retained.Inventory == 1, "prior facts survive a later trap");
    Require(retained.OwnedCount() == 0 && retained.PendingCount() == 0 && retained.CleanupCount() == 1, "fault cleans owned work");

    FakeHost stopped(Start(Program("process 1; emit bolt(1, speed(1), ticks(100)); stop;")));
    Advance(stopped, 1);
    Require(stopped.Trace().size() == 1 && stopped.OwnedCount() == 0, "stop applies final slice then cleans ownership");
    FakeHost cancelled(Start(Program("process 1; emit bolt(1, speed(1), ticks(100)); wait ticks(100);")));
    Advance(cancelled, 1);
    cancelled.Cancel(); cancelled.Cancel();
    Require(cancelled.CleanupCount() == 1 && cancelled.OwnedCount() == 0 && cancelled.PendingCount() == 0, "external cancel cleanup");
}
void AwaitTerminals() {
    for (const auto terminal : {Outcome::Hit, Outcome::Expired, Outcome::Rejected, Outcome::Cancelled}) {
        auto instance = Start(Program("process 1; emit bolt(1, speed(1), ticks(3)); await hit within ticks(2); emit dig(cells(1));"));
        auto result = instance->Step({0, 0});
        const auto event = Event(1, 0, result.Requests[0].Id, terminal);
        result = instance->Step({1, 0}, {&event, 1});
        Require(result.Count == 1 && instance->State().LastOutcome == terminal && result.End, "hit wait handles every terminal");
    }
    for (const auto terminal : {Outcome::Applied, Outcome::Rejected, Outcome::Cancelled}) {
        auto instance = Start(Program("process 1; emit dig(cells(1)); await result within ticks(2);"));
        auto result = instance->Step({0, 0});
        const auto event = Event(1, 0, result.Requests[0].Id, terminal);
        result = instance->Step({1, 0}, {&event, 1});
        Require(instance->State().LastOutcome == terminal && result.End, "result wait handles every receipt");
    }
    auto timeout = Start(Program("process 1; await wake within ticks(2); emit dig(cells(1));"));
    timeout->Step({0, 0});
    Require(timeout->Step({1, 0}).Count == 0, "await timeout not early");
    Require(timeout->Step({2, 0}).Count == 1 && timeout->State().LastOutcome == Outcome::Timeout, "await timeout resumes");

    auto deadline = Start(Program("process 1; await wake within ticks(1);"));
    deadline->Step({0, 0});
    HostEvent wake{1, 0, 1, Outcome::Wake, {}, *deadline->State().WakeSubscription, {}};
    Require(deadline->Step({1, 0}, {&wake, 1}).End.has_value() && deadline->State().LastOutcome == Outcome::Wake, "event wins at timeout boundary");
    for (auto terminal : {Outcome::Rejected, Outcome::Cancelled}) {
        auto subscription = Start(Program("process 1; await wake within ticks(3);"));
        subscription->Step({0, 0});
        wake.Result = terminal;
        wake.WakeSubscription = *subscription->State().WakeSubscription;
        Require(subscription->Step({1, 0}, {&wake, 1}).End.has_value() && subscription->State().LastOutcome == terminal,
                "subscription refusal/cancellation is terminal");
    }

    FakeHost expired(Start(Program(Example("receipts"))));
    Advance(expired, 8);
    Require(expired.Trace().size() == 2 && std::holds_alternative<Wind>(expired.Trace()[1].Request.Description), "fake carrier expiry takes failure branch");
    FakeHost rejected(Start(Program(Example("receipts"))));
    rejected.RejectEffects = true;
    Advance(rejected, 8);
    Require(rejected.Trace().size() == 1 && rejected.Process().State().Status == Phase::Ended, "host refusal does not hang");
}
void EarlyRepliesAndIsolation() {
    auto instance = Start(Program(R"(process 1;
        emit bolt(1, speed(1), ticks(20)); wait ticks(2);
        await result within ticks(3); await hit within ticks(3); emit dig(cells(1));
    )"));
    auto result = instance->Step({0, 0});
    const auto request = result.Requests[0].Id;
    const std::array events{Event(1, 0, request, Outcome::Applied), Event(2, 0, request, Outcome::Hit, {7, 0, 0, 0})};
    instance->Step({1, 0}, events);
    Require(instance->Step({2, 0}).Count == 0, "await always creates a commit boundary");
    instance->Step({3, 0});
    result = instance->Step({4, 0});
    Require(result.Count == 1 && result.Requests[0].Origin.CellX == 7, "early receipt and hit survive ordinary wait");

    auto stale = Start(Program(R"(process 1;
        emit bolt(1, speed(1), ticks(20)); wait ticks(1);
        emit bolt(2, speed(1), ticks(20)); await hit within ticks(1);
        if outcome() == timeout { emit dig(cells(1)); }
    )"));
    result = stale->Step({0, 0});
    auto wrong = Event(1, 0, result.Requests[0].Id, Outcome::Hit);
    stale->Step({1, 0});
    result = stale->Step({2, 0}, {&wrong, 1});
    Require(result.Count == 1 && stale->State().LastOutcome == Outcome::Timeout, "previous request cannot wake latest request");

    auto duplicates = Start(Program(R"(process 1;
        emit bolt(1, speed(1), ticks(20)); await hit within ticks(2);
        await hit within ticks(1); if outcome() == timeout { emit dig(cells(1)); }
    )"));
    result = duplicates->Step({0, 0});
    auto hit = Event(1, 0, result.Requests[0].Id, Outcome::Hit);
    duplicates->Step({1, 0}, {&hit, 1});
    hit.Sequence = 2; hit.CommittedAt = 1;
    result = duplicates->Step({2, 0}, {&hit, 1});
    Require(result.Count == 1 && duplicates->State().LastOutcome == Outcome::Timeout, "terminal cannot be consumed twice");

    auto subscriptions = Start(Program("process 1; await wake within ticks(1); await wake within ticks(1);"));
    subscriptions->Step({0, 0});
    HostEvent oldWake{1, 0, 1, Outcome::Wake, {}, *subscriptions->State().WakeSubscription, {}};
    subscriptions->Step({1, 0});
    subscriptions->Step({2, 0}, {&oldWake, 1});
    Require(subscriptions->State().LastOutcome == Outcome::Timeout, "expired subscription cannot wake new subscription");
}
void HostBoundaries() {
    FakeHost saturated(Start(Program(R"(process 1;
        while true {
            var n = 0;
            while n < 64 { emit dig(cells(1)); n = n + 1; }
            wait ticks(1);
        }
    )"), {4096, 0, 64}));
    Advance(saturated, 100);
    Require(saturated.Inventory == 6400 && saturated.Trace().size() == 6400 && saturated.PendingCount() == 1,
            "host accepts maximum legal output without accumulating unreachable receipts");
    saturated.Cancel();
    Require(saturated.PendingCount() == 0 && saturated.CleanupCount() == 1, "saturated host cleanly cancels");

    FakeHost inactive(Start(Program("process 1; emit bolt(1, speed(1), ticks(10)); wait ticks(2); emit dig(cells(1));")));
    Advance(inactive, 1);
    inactive.Skip(10000);
    Advance(inactive, 1);
    Require(inactive.Trace().size() == 1 && inactive.OwnedCount() == 1, "inactive boundaries freeze process and carrier");
    Advance(inactive, 1);
    Require(inactive.Trace().size() == 2 && inactive.Trace()[1].At == 10002, "active time resumes without catchup");

    const auto program = Program("process 1; if inventory() < 1 { emit dig(cells(1)); } wait ticks(1);");
    auto first = Start(program, {}, 1), second = Start(program, {}, 2);
    const HostView frozen{0, 0};
    const auto a = first->Step(frozen), b = second->Step(frozen);
    Require(a.Count == 1 && b.Count == 1 && a.QueriesUsed == 1 && b.QueriesUsed == 1, "all instances read frozen state before effects");
    Throws<std::invalid_argument>([&] { first->Step(frozen); }, "duplicate active boundary is a host bug");
    auto sameFrame = Start(Program("process 1; await wake within ticks(3);"));
    sameFrame->Step({0, 0});
    HostEvent future{1, 1, 1, Outcome::Wake, {}, *sameFrame->State().WakeSubscription, {}};
    Throws<std::invalid_argument>([&] { sameFrame->Step({1, 0}, {&future, 1}); }, "same-boundary event breaks phase ordering");

    auto overloaded = Start(Program("process 1; wait ticks(2);"));
    overloaded->Step({0, 0});
    std::array<HostEvent, 33> events{};
    const auto result = overloaded->Step({1, 0}, events);
    Require(result.Fault && result.Count == 0 && result.End, "event batch bound terminates without growing inbox");
}
void MalformedCorpus() {
    // Deterministic parser robustness corpus, independent of compiler internals.
    std::mt19937 random(32771);
    constexpr std::string_view characters = "abc123_{}();,=+<!- \n/.$";
    for (int sample = 0; sample < 4000; ++sample) {
        std::string text = sample % 2 ? "process 1; " : "";
        const auto size = random() % 256;
        for (std::size_t i = 0; i < size; ++i) text += characters[random() % characters.size()];
        const auto result = Compile("corpus", text);
        if (const auto *program = std::get_if<ProgramArtifact>(&result)) Start(*program)->Step({0, 0});
    }
}
} // namespace
int main() {
    try {
        GoldenScenarios(); IdentityAndPublication(); DiagnosticsAndBounds(); ValuesAndControl();
        BudgetTransactions(); AwaitTerminals(); EarlyRepliesAndIsolation(); HostBoundaries(); MalformedCorpus();
        std::cout << "process runtime tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
