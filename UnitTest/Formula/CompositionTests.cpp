#include "Formula/Process/Editing.h"
#include "ProcessFakeHost.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace pixel::formula::process;
namespace {
void Check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
ProgramArtifact Program(std::string_view source) {
    auto result = Compile("composition-test", source);
    if (const auto *error = std::get_if<Diagnostic>(&result))
        throw std::runtime_error(std::to_string(error->Source.Line) + ": " + error->Message);
    return std::get<ProgramArtifact>(std::move(result));
}
std::unique_ptr<Instance> Start(const ProgramArtifact &program, InstanceId id = 1, Position origin = {}) {
    return std::get<std::unique_ptr<Instance>>(Instance::Start(program, id, AllCapabilities, {}, origin));
}
const auto Encapsulated = R"(formula 2;
fn blast(radius: cells) -> action = dig(radius);
fn shot(impact: action) -> action = on_hit(bolt(10, speed(60), ticks(4)), impact);
fn twice(body: action) -> action = repeat(2, ticks(2), body);
let bound = shot(blast(cells(2)));
run twice(bound);
)";
const auto Expanded = R"(formula 2;
run repeat(2, ticks(2), on_hit(bolt(10, speed(60), ticks(4)), dig(cells(2))));
)";

void EncapsulationAndIsolation() {
    const auto program = Program(Encapsulated);
    Check(program.Identity().SemanticsVersion == 2 && program.Identity().HostContractVersion == 3,
        "composition identity lacks language/host version");
    Check(program.Source() == Encapsulated, "generated source escaped into the artifact");
    Check(std::get<ProgramArtifact>(ImportProgram(program.Export())).Identity() == program.Identity(), "source record round trip failed");
    testing::FakeHost wrapped(Start(program)), expanded(Start(Program(Expanded)));
    Check(wrapped.Trace().empty() && wrapped.OwnedCount() == 0, "construction executed an effect");
    for (int tick = 0; tick < 30; ++tick) {
        if (wrapped.OwnedCount()) { wrapped.Hit({7, 8, .25, .75}); expanded.Hit({7, 8, .25, .75}); }
        Check(!wrapped.Advance().Fault && !expanded.Advance().Fault, "composition execution faulted");
    }
    const auto &a = wrapped.Trace(), &b = expanded.Trace();
    Check(a.size() == 4 && a.size() == b.size(), "encapsulation changed request count");
    for (size_t i = 0; i < a.size(); ++i)
        Check(a[i].At == b[i].At && a[i].Request.Id == b[i].Request.Id && a[i].Request.Origin == b[i].Request.Origin
            && a[i].Request.Description == b[i].Request.Description && a[i].Result == b[i].Result,
            "encapsulation changed observable request order/context");
    Check(a[2].Request.Origin == Position{}, "second projectile inherited the previous hit point");

    auto first = Start(program, 11, {1, 2}), second = Start(program, 12, {9, 10});
    const auto one = first->Step({0, 6}), two = second->Step({0, 6});
    Check(one.Count == 1 && two.Count == 1 && one.Requests[0].Id.Instance == 11 && two.Requests[0].Id.Instance == 12
        && two.Requests[0].Origin.CellX == 9, "shared description leaked mutable state");
    first->Cancel();
    Check(second->State().Status == Phase::Awaiting, "cancellation leaked to another instance");
}

void TypesAndBounds() {
    for (const auto source : {
        "formula 2; fn f(x: cells) -> action = dig(x); run f(speed(2));",
        "formula 2; fn f(x: cells) -> action = dig(x); run f();",
        "formula 2; fn f(x: cells) -> speed = dig(x); run nothing();",
        "formula 2; fn f() -> action = f(); run f();",
        "formula 2; let x = cells(2); fn f(x: cells) -> action = dig(x); run f(x);",
        "formula 2; fn f(x: cells) -> action = dig(x); run dig(x);",
        "formula 2; run on_hit(dig(cells(2)), nothing());",
        "formula 2; run delay(ticks(0), nothing());",
        "formula 2; run repeat(1.5, ticks(1), nothing());",
        "formula 2; run repeat(65, ticks(1), nothing());",
        "formula 2; run dig(cells(-1));",
        "formula 2; run system(1);",
        "formula 2; run scope_begin();",
        "formula 2; run field(cells(2), speed(2), speed(0), ticks(1));",
        "formula 3; run nothing();"})
        Check(std::holds_alternative<Diagnostic>(Compile("invalid", source)), "invalid formula accepted");
    Check(std::holds_alternative<Diagnostic>(Compile("denied", Encapsulated, Mask(Capability::Wind))), "capability bypass in user function");
    auto valid = Program("formula 2; fn size(x: number) -> cells = cells(multiply(x, 2)); run dig(size(1.5));");
    const auto request = Start(valid)->Step({0, 0});
    Check(std::get<Dig>(request.Requests[0].Description).Radius == 3, "typed scalar return/parameter binding failed");
    Check(request.Requests[0].Source.Offset < valid.Source().size(), "request source is generated code");
    const auto error = std::get<Diagnostic>(Compile("source", "formula 2;\nrun dig(speed(2));"));
    Check(error.Source.Line == 2, "type error lost user source location");
    std::string expansion = "formula 2; fn twice(a: action) -> action = sequence(a,a); run ";
    for (int i = 0; i < 12; ++i) expansion += "twice(";
    expansion += "dig(cells(1))" + std::string(12, ')') + ";";
    Check(std::holds_alternative<Diagnostic>(Compile("bounded", expansion)), "expanded description bypassed compiler limits");
}

HostEvent Event(std::uint64_t sequence, Boundary boundary, Outcome outcome, RequestId request, Position hit = {}) {
    return {sequence, boundary, request.Instance, outcome, request, {}, hit};
}
void HitScopesAndRestore() {
    const auto program = Program(R"(formula 2;
run sequence(on_hit(bolt(1, speed(60), ticks(10)), delay(ticks(2), dig(cells(0)))), dig(cells(1)));
)");
    auto instance = Start(program, 1, {1, 1});
    auto output = instance->Step({0, 10, Position{2, 2}});
    const auto hit = Event(1, 0, Outcome::Hit, output.Requests[0].Id, {7, 8, .2, .3});
    instance->Step({1, 10, Position{3, 3}}, std::span(&hit, 1));
    const auto state = instance->ExportState();
    Check(state.SchemaVersion == 3 && state.OriginPinned && state.OriginScopes.size() == 1, "hit scope missing from checkpoint");
    auto broken = state; broken.OriginScopes.clear();
    Check(std::holds_alternative<Diagnostic>(Instance::Restore(program, broken)), "invalid continuation scope accepted");
    broken = state; broken.OriginPinned = false;
    Check(std::holds_alternative<Diagnostic>(Instance::Restore(program, broken)), "saved hit scope silently changed to a live binding");
    broken = state; broken.OriginScopes[0].Pinned = true;
    Check(std::holds_alternative<Diagnostic>(Instance::Restore(program, broken)), "saved outer binding silently became pinned");
    instance.reset();
    instance = std::get<std::unique_ptr<Instance>>(Instance::Restore(program, state));
    instance->Step({2, 10, Position{4, 4}});
    output = instance->Step({3, 10, Position{5, 5}});
    Check(output.Count == 1 && output.Requests[0].Origin == hit.HitPosition, "delayed hit effect followed moving actor");
    const auto applied = Event(2, 3, Outcome::Applied, output.Requests[0].Id);
    output = instance->Step({4, 10, Position{6, 6}}, std::span(&applied, 1));
    Check(output.Count == 1 && output.Requests[0].Origin == Position{6, 6}, "completed child did not restore live outer binding");
    auto saved = instance->ExportState();
    Check(!saved.OriginPinned && saved.OriginScopes.empty(), "completed child retained its context");
}

void OutcomesBudgetsAndEditing() {
    const auto program = Program("formula 2; run sequence(dig(cells(1)), dig(cells(2)));");
    testing::FakeHost rejected(Start(program)); rejected.RejectEffects = true;
    rejected.Advance(); rejected.Advance();
    Check(rejected.Trace().size() == 1 && rejected.Process().State().Status == Phase::Ended
        && rejected.Process().State().LastOutcome == Outcome::Rejected, "failure did not stop dependent calls");
    auto limited = std::get<std::unique_ptr<Instance>>(Instance::Start(program, 1, AllCapabilities, {2, 1, 1}));
    const auto fault = limited->Step({0, 1});
    Check(fault.Fault && fault.Count == 0 && fault.End, "work budget exposed partial output");
    testing::FakeHost condition(Start(Program("formula 2; run if_credits(2, dig(cells(3)), on_wake(ticks(3), dig(cells(1))));")));
    condition.Advance(); condition.Wake(); condition.Skip(5); condition.Advance();
    Check(condition.Trace().size() == 1 && std::get<Dig>(condition.Trace()[0].Request.Description).Radius == 1,
        "query/wake branch failed");
    auto old = Start(program); old->Step({0, 0});
    const auto newProgram = Program("formula 2; run dig(cells(9));");
    Check(std::holds_alternative<Diagnostic>(Instance::Restore(newProgram, old->ExportState())), "edit rebound old continuation");
    testing::FakeHost field(Start(Program("formula 2; run field(cells(2), acceleration(1), acceleration(0), ticks(3));")));
    field.Advance(); field.Advance(); field.Suspend(); field.Skip(100); Check(!field.Resume(), "field resume failed");
    field.Advance(); field.Advance();
    Check(field.Trace().size() == 1 && field.OwnedCount() == 0 && field.Process().State().Status == Phase::Ended,
        "field lifetime or suspended ownership changed");
}

void MalformedCorpus() {
    std::mt19937 random(8732);
    const std::string base = Encapsulated;
    constexpr std::string_view characters = "fn();:->0123/abc \n";
    for (int i = 0; i < 2000; ++i) {
        auto source = base;
        for (int j = 0; j < 1 + i % 4; ++j) source[random() % source.size()] = characters[random() % characters.size()];
        const auto result = Compile("corpus", source);
        if (const auto *program = std::get_if<ProgramArtifact>(&result)) Start(*program)->Step({0, 6});
    }
}
void Benchmark() {
    using Clock = std::chrono::steady_clock;
    const auto composed = Program(Encapsulated), expanded = Program(Expanded);
    for (int trial = 0; trial < 3; ++trial) {
        const auto start = Clock::now();
        for (int i = 0; i < 500; ++i) Program(Encapsulated);
        const auto compileUs = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / 500;
        for (const auto *program : {&composed, &expanded}) {
            std::vector<double> samples;
            std::uint64_t work = 0;
            for (int i = 0; i < 2000; ++i) {
                auto instance = Start(*program);
                const auto before = Clock::now();
                const auto result = instance->Step({0, 6});
                samples.push_back(std::chrono::duration<double, std::micro>(Clock::now() - before).count());
                work += result.WorkUsed;
            }
            std::sort(samples.begin(), samples.end());
            std::cout << "trial=" << trial + 1 << " form=" << (program == &composed ? "wrapped" : "expanded")
                << " compile_us=" << compileUs << " start_slice_p50_us=" << samples[1000]
                << " p95_us=" << samples[1900] << " work=" << work / 2000 << " instructions=" << program->InstructionCount() << '\n';
        }
    }
}
} // namespace
int main(int argc, char **) {
    try {
        EncapsulationAndIsolation(); TypesAndBounds(); HitScopesAndRestore(); OutcomesBudgetsAndEditing(); MalformedCorpus();
        if (argc > 1) Benchmark();
        std::cout << "formula/2 composition tests passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
