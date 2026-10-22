#include "Formula/Process/Editing.h"
#include "ProcessFakeHost.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace pixel::formula::process;
using testing::FakeHost;
namespace {
void Check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Function> void HostError(Function function) {
    try { function(); } catch (const std::logic_error &) { return; }
    throw std::runtime_error("missing host contract failure");
}
ProgramArtifact Program(std::string_view source) {
    auto result = Compile("state.process", source);
    if (const auto *error = std::get_if<Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<ProgramArtifact>(std::move(result));
}
std::unique_ptr<Instance> Start(const ProgramArtifact &program) {
    return std::get<std::unique_ptr<Instance>>(Instance::Start(program, 7));
}
std::unique_ptr<Instance> Restore(const ProgramArtifact &program, const InstanceSnapshot &snapshot) {
    auto result = Instance::Restore(program, snapshot);
    if (const auto *error = std::get_if<Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<std::unique_ptr<Instance>>(std::move(result));
}
void Advance(FakeHost &host, int count = 1) {
    for (int i = 0; i < count; ++i) Check(!host.Advance().Fault, "restored process faulted");
}
void ProgramRecords() {
    const auto program = Program("process 1; wait ticks(2);");
    const auto record = program.Export();
    Check(std::get<ProgramArtifact>(ImportProgram(record)).Identity() == program.Identity(), "program round trip changed identity");
    auto invalid = record;
    invalid.SchemaVersion = 2;
    Check(std::holds_alternative<Diagnostic>(ImportProgram(invalid)), "record version accepted");
    invalid = record; invalid.Identity.SemanticsVersion = 2;
    Check(std::holds_alternative<Diagnostic>(ImportProgram(invalid)), "language version accepted");
    invalid = record; invalid.Identity.HostContractVersion = 2;
    Check(std::holds_alternative<Diagnostic>(ImportProgram(invalid)), "host version accepted");
    invalid = record; invalid.Source += " ";
    Check(std::holds_alternative<Diagnostic>(ImportProgram(invalid)), "source identity mismatch accepted");
    invalid = record; invalid.Source = std::string(16385, ' ');
    Check(std::holds_alternative<Diagnostic>(ImportProgram(invalid)), "import bypassed source budget");
}
void ValuesAndValidation() {
    const auto program = Program(R"(process 1;
        let a = true; let b = 3; let c = 2.5; let d = cells(2);
        let e = speed(4); let f = ticks(2); let g = bolt(8, e, f); let h = outcome();
        if false { let skipped = 9; wait ticks(1); }
        wait f; if a { emit g; } wait ticks(3);
    )");
    auto instance = Start(program);
    HostError([&] { instance->ExportState(); });
    instance->Step({0, 0});
    const auto snapshot = instance->ExportState();
    for (std::size_t i = 0; i < 8; ++i) Check(snapshot.Locals[i] && snapshot.Locals[i]->index() == i, "typed local missing");
    Check(!snapshot.Locals[8], "out-of-scope local leaked into snapshot");
    instance.reset();
    instance = Restore(program, snapshot);
    Check(instance->Step({1000, 0}).Count == 0 && instance->State().Remaining == 1, "wall-clock gap aged delay");
    const auto result = instance->Step({1001, 0});
    Check(result.Count == 1 && std::get<Bolt>(result.Requests[0].Description) == Bolt{8, 4, 2}, "typed values did not restore");
    const auto reject = [&](InstanceSnapshot state) {
        Check(std::holds_alternative<Diagnostic>(Instance::Restore(program, state)), "invalid snapshot accepted");
    };
    auto bad = snapshot; bad.SchemaVersion = 2; reject(bad);
    bad = snapshot; bad.Program.ContentSha256 = "other"; reject(bad);
    bad = snapshot; bad.State.Instruction = 0; reject(bad);
    bad = snapshot; bad.State.Status = Phase::Ready; reject(bad);
    bad = snapshot; bad.State.Remaining = 0; reject(bad);
    bad = snapshot; bad.State.Id = 0; reject(bad);
    bad = snapshot; bad.Limits.Work = 0; reject(bad);
    bad = snapshot; bad.Locals[0].reset(); reject(bad);
    bad = snapshot; bad.Locals[1] = true; reject(bad);
    bad = snapshot; bad.Locals[8] = std::int64_t(9); reject(bad);
    bad = snapshot; bad.Locals[2] = std::numeric_limits<double>::infinity(); reject(bad);
    bad = snapshot; bad.Locals[6] = Effect{Bolt{8, 4, 0}}; reject(bad);
    bad = snapshot; bad.State.Origin.OffsetX = 1; reject(bad);
    Check(std::holds_alternative<Diagnostic>(Instance::Restore(program, snapshot, 0)), "restore bypassed capabilities");
    instance->Cancel();
    HostError([&] { instance->ExportState(); });
}
void NoReplayAndFrozenLifetime() {
    FakeHost host(Start(Program("process 1; emit dig(cells(1)); emit bolt(8, speed(1), ticks(3)); await hit within ticks(8); wait ticks(2);")));
    Advance(host);
    Check(host.Inventory == 1 && host.OwnedCount() == 1, "initial effects missing");
    host.Suspend();
    Check(host.OwnedCount() == 0 && host.CleanupCount() == 0 && host.Suspension().Carriers[0].Remaining == 3,
        "suspension cancelled or left active effects");
    const auto before = host.Suspension().Runtime.State;
    host.Skip(10000);
    Advance(host, 3);
    Check(host.Suspension().Runtime.State.ActiveSteps == before.ActiveSteps && host.Suspension().Carriers[0].Remaining == 3,
        "frozen process aged");
    Check(host.Resume(0).has_value() && host.Inventory == 1, "failed resume changed world");
    Check(!host.Resume(), "resume refused valid capsule");
    HostError([&] { host.Resume(); });
    Advance(host, 2);
    Check(host.OwnedCount() == 1 && host.Inventory == 1 && host.Trace().size() == 2, "resume replayed facts or aged carrier too far");
    Advance(host);
    Check(host.OwnedCount() == 0, "restored remaining lifetime did not expire");
    Advance(host);
    Check(host.Process().State().LastOutcome == Outcome::Expired, "expiry event lost across restore");
    host.Cancel(); host.Cancel();
    Check(host.CleanupCount() == 1 && host.Inventory == 1 && host.PendingCount() == 0, "cancel undid facts or repeated cleanup");
}
void CachedEventsAndWake() {
    FakeHost host(Start(Program(R"(process 1;
        emit bolt(8, speed(1), ticks(12)); wait ticks(2);
        await result within ticks(3); await hit within ticks(3);
        if outcome() == hit { emit dig(cells(2)); } wait ticks(2);
    )")));
    Advance(host);
    host.Hit({9007199254740993LL, -7, .25, .5});
    Advance(host, 2); // receipt and impact are cached, now at await result.
    host.Suspend();
    Check(host.Suspension().Runtime.Receipt && host.Suspension().Runtime.Impact, "early reply caches missing");
    host.Skip(1000);
    Check(!host.Resume(), "cached-event resume failed");
    Advance(host, 2);
    Check(host.Trace().size() == 2 && host.Trace().back().Request.Origin == Position{9007199254740993LL, -7, .25, .5},
        "restored hit lost precision or continuation");

    FakeHost wake(Start(Program("process 1; await wake within ticks(5); emit dig(cells(1)); await wake within ticks(3); wait ticks(2);")));
    Advance(wake);
    const auto subscription = *wake.Process().State().WakeSubscription;
    wake.Wake();
    wake.Suspend(); wake.Skip(2000);
    Check(!wake.Resume() && wake.Process().State().WakeSubscription == subscription, "wake identity changed");
    Advance(wake);
    Check(wake.Inventory == 1 && wake.Process().State().WakeSubscription->Sequence == subscription.Sequence + 1,
        "pending wake lost or generation reused");
    wake.Suspend(); wake.Skip(1000); Check(!wake.Resume(), "second wake resume failed");
    Advance(wake, 2);
    Check(wake.Process().State().Remaining == 1 && wake.Inventory == 1, "wake timeout caught up frozen time");
    Advance(wake);
    Check(wake.Process().State().LastOutcome == Outcome::Timeout && !wake.Process().State().WakeSubscription,
        "wake timeout did not retire subscription");

    const auto program = Program("process 1; emit dig(cells(1)); await result within ticks(2); await result within ticks(2); wait ticks(1);");
    auto instance = Start(program);
    const auto request = instance->Step({0, 0}).Requests[0].Id;
    HostEvent receipt{1, 0, 7, Outcome::Applied, request};
    instance->Step({1, 0}, {&receipt, 1});
    auto snapshot = instance->ExportState();
    Check(snapshot.ReceiptSeen && !snapshot.Receipt, "consumed receipt state missing");
    instance.reset(); instance = Restore(program, snapshot);
    receipt.Sequence = 2; receipt.CommittedAt = 1;
    instance->Step({10, 0}, {&receipt, 1});
    Check(instance->State().Status == Phase::Awaiting && instance->State().Remaining == 1, "consumed receipt replay woke continuation");
    instance->Step({11, 0});
    Check(instance->State().LastOutcome == Outcome::Timeout, "replayed receipt changed outcome");
}
void OwnershipAndRepeatedRoundTrips() {
    FakeHost entities(Start(Program("process 1; emit bolt(1, speed(0), ticks(8)); emit bolt(2, speed(0), ticks(4)); wait ticks(3);")));
    Advance(entities);
    entities.ReleaseCarrier(entities.Trace()[0].Request.Id);
    entities.Suspend();
    entities.Cancel(); entities.Cancel();
    Check(entities.CleanupCount() == 1 && entities.IndependentCount() == 1, "suspended cleanup deleted independent entity");
    Advance(entities, 8);
    Check(entities.IndependentCount() == 0, "independent entity did not follow world lifetime");

    const auto program = Program(R"(process 1; var n = 0;
        while n < 12 { let radius = cells(n); emit dig(radius);
            wait ticks(2); await result within ticks(2); n = n + 1; }
    )");
    FakeHost continuous(Start(program)), restored(Start(program));
    for (int i = 0; i < 50; ++i) {
        Advance(continuous); Advance(restored);
        if (restored.Process().State().Status != Phase::Ended) {
            restored.Suspend(); restored.Skip(97); Check(!restored.Resume(), "repeated round trip failed");
        }
        Check(continuous.Process().State().ActiveSteps == restored.Process().State().ActiveSteps, "active clock diverged");
        Check(continuous.Trace().size() == restored.Trace().size() && continuous.Inventory == restored.Inventory, "round trip changed behavior");
    }
    Check(restored.Trace().size() == 12, "loop continuation lost locals");
    for (std::size_t i = 0; i < restored.Trace().size(); ++i)
        Check(continuous.Trace()[i].Request.Description == restored.Trace()[i].Request.Description
            && restored.Trace()[i].Request.Id.Sequence == i + 1, "round trip changed value or reused request sequence");
}
void EditingPublication() {
    editing::Draft draft("editable.process");
    draft.SetSource("process 1; wait ticks(1); emit dig(cells(1));");
    Check(draft.Build() && draft.Program(), "initial draft did not compile");
    const auto old = *draft.Program();
    FakeHost running(Start(old)); Advance(running); running.Suspend();
    draft.SetSource("process 1; emit dig(cells(9));");
    Check(!draft.Program() && !draft.Error(), "text edit retained stale compiled artifact");
    Check(draft.Build() && draft.Program()->Identity() != old.Identity(), "edit did not publish new artifact");
    Check(std::holds_alternative<Diagnostic>(Instance::Restore(*draft.Program(), running.Suspension().Runtime)), "cross-version state migration was implicit");
    FakeHost fresh(Start(*draft.Program())); Advance(fresh);
    Check(!running.Resume(), "old artifact was not retained"); Advance(running);
    Check(std::get<Dig>(running.Trace()[0].Request.Description).Radius == 1
        && std::get<Dig>(fresh.Trace()[0].Request.Description).Radius == 9, "Apply changed old instance");
    draft.SetSource("process 1;\nwait ticks(false);");
    Check(!draft.Build() && !draft.Program() && draft.Error()->Source.Line == 2, "bad edit retained publication or lost source diagnostic");
}
} // namespace
int main() {
    try {
        ProgramRecords(); ValuesAndValidation(); NoReplayAndFrozenLifetime(); CachedEventsAndWake();
        OwnershipAndRepeatedRoundTrips(); EditingPublication();
        std::cout << "Process state and editing contracts passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
