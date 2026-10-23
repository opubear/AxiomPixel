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
ProgramArtifact Program(std::string_view source) {
    auto result = Compile("field.process", source);
    if (const auto *error = std::get_if<Diagnostic>(&result)) throw std::runtime_error(error->Message);
    return std::get<ProgramArtifact>(std::move(result));
}
std::unique_ptr<Instance> Start(const ProgramArtifact &program) {
    return std::get<std::unique_ptr<Instance>>(Instance::Start(program, 7));
}
void Advance(FakeHost &host, int count = 1) {
    for (int i = 0; i < count; ++i) Check(!host.Advance().Fault, "field process faulted");
}
void HandleRoundTrip() {
    FakeHost host(Start(Program(R"(process 1;
        emit field(cells(8), acceleration(3600), acceleration(-30), ticks(20));
        await result within ticks(2);
        let h = effect_handle();
        emit update_field(h, field(cells(4), acceleration(-1800), acceleration(0), ticks(10)));
        await result within ticks(2);
        emit stop_field(h); await result within ticks(2);
        emit stop_field(effect_handle()); await result within ticks(2);
        wait ticks(2);
    )")));
    Advance(host);
    Check(host.FieldSources().size() == 1, "installation did not create an owned source");
    const auto token = host.FieldSources()[0].Handle;
    Advance(host);
    Check(host.Process().State().LastEffectHandle == token
        && std::get<UpdateField>(host.Trace().back().Request.Description).Handle == token
        && host.FieldSources()[0].Source == FieldSource{4, -1800, 0, 10}, "receipt/local/update lost source identity or units");
    Advance(host);
    Check(host.Process().State().LastEffectHandle == token && host.OwnedCount() == 0, "update changed token or stop left its source");
    Advance(host, 2);
    Check(host.Trace().back().Result == Outcome::Rejected
        && host.Process().State().LastOutcome == Outcome::Rejected
        && host.Process().State().LastEffectHandle.Id == 0, "stop receipt exposed a stale handle");
}
void TypesAndFaults() {
    constexpr std::array invalid{
        "process 1; emit field(cells(1), speed(2), acceleration(0), ticks(3));",
        "process 1; emit field(cells(1), acceleration(2), speed(0), ticks(3));",
        "process 1; emit field(cells(1), acceleration(2), acceleration(0), 3);",
        "process 1; emit stop_field(1);",
        "process 1; emit update_field(1, dig(cells(1)));",
        "process 1; let h = effect_handle(1);",
        "process 1; emit acceleration(1);"};
    for (const auto source : invalid)
        Check(std::holds_alternative<Diagnostic>(Compile("invalid", source)), "field unit or handle type confusion accepted");
    Check(std::holds_alternative<Diagnostic>(Compile("denied",
        "process 1; emit field(cells(1), acceleration(2), acceleration(0), ticks(3));",
        AllCapabilities & ~Mask(Capability::Field))), "field capability was not enforced");
    auto bad = Start(Program("process 1; emit dig(cells(1)); emit update_field(effect_handle(), dig(cells(2)));"));
    const auto result = bad->Step({0, 0});
    Check(result.Fault && result.Count == 0 && result.End->Reason == EndReason::Faulted,
        "non-field update did not fault atomically");
}
void AwaitResetAndValidation() {
    const auto program = Program(R"(process 1;
        emit field(cells(1), acceleration(2), acceleration(0), ticks(20));
        await result within ticks(2); let saved = effect_handle();
        await wake within ticks(1); emit stop_field(saved); wait ticks(2);
    )");
    auto instance = Start(program);
    const auto request = instance->Step({0, 0}).Requests[0].Id;
    HostEvent event{1, 0, 7, Outcome::Applied, request, {}, {}, {83}};
    instance->Step({1, 0}, {&event, 1});
    const auto result = instance->Step({2, 0});
    Check(instance->State().LastEffectHandle.Id == 0 && instance->State().LastOutcome == Outcome::Timeout
        && std::get<StopField>(result.Requests[0].Description).Handle.Id == 83,
        "later await did not clear transient handle or preserve local token");

    instance = Start(program);
    instance->Step({0, 0});
    event.Result = Outcome::Rejected;
    bool rejected = false;
    try { instance->Step({1, 0}, {&event, 1}); } catch (const std::invalid_argument &) { rejected = true; }
    Check(rejected, "rejected receipt with a handle violated host contract silently");
}
void SnapshotValues() {
    const auto program = Program(R"(process 1;
        let a = acceleration(-12);
        let f = field(cells(3), a, acceleration(0), ticks(20));
        emit f; await result within ticks(2);
        let h = effect_handle(); let u = update_field(h, f); let s = stop_field(h);
        wait ticks(2); emit u; await result within ticks(2); emit s; wait ticks(2);
    )");
    auto instance = Start(program);
    const auto request = instance->Step({0, 0}).Requests[0].Id;
    HostEvent event{1, 0, 7, Outcome::Applied, request, {}, {}, {93}};
    instance->Step({1, 0}, {&event, 1});
    const auto snapshot = instance->ExportState();
    Check(snapshot.Locals[0]->index() == 8 && snapshot.Locals[2]->index() == 9,
        "new value kinds changed old variant indexes");
    const auto refuse = [&](InstanceSnapshot candidate) {
        Check(std::holds_alternative<Diagnostic>(Instance::Restore(program, candidate)), "invalid field snapshot accepted");
    };
    auto bad = snapshot; bad.Locals[0] = Acceleration{std::numeric_limits<double>::infinity()}; refuse(bad);
    bad = snapshot; bad.Locals[1] = Effect{FieldSource{3, 1, 0, 0}}; refuse(bad);
    bad = snapshot; bad.Locals[2] = std::int64_t(93); refuse(bad);
    bad = snapshot; bad.Locals[3] = Effect{UpdateField{{93}, {3, 0, std::numeric_limits<double>::quiet_NaN(), 5}}}; refuse(bad);
    bad = snapshot; bad.State.LastOutcome = Outcome::Rejected; refuse(bad);
    instance.reset();
    instance = std::get<std::unique_ptr<Instance>>(Instance::Restore(program, snapshot));
    instance->Step({1000, 0});
    const auto updated = instance->Step({1001, 0});
    Check(updated.Count == 1 && std::get<UpdateField>(updated.Requests[0].Description) == UpdateField{{93}, {3, -12, 0, 20}},
        "restored source/update/handle values changed");
}
void HostLifecycle() {
    FakeHost host(Start(Program(R"(process 1;
        emit dig(cells(1));
        emit field(cells(1), acceleration(12), acceleration(0), ticks(3));
        await result within ticks(2); let h = effect_handle();
        wait ticks(4); emit update_field(h, field(cells(1), acceleration(2), acceleration(0), ticks(3)));
        wait ticks(3);
    )")));
    Advance(host, 2);
    host.Suspend();
    Check(host.OwnedCount() == 0 && host.Suspension().Fields.size() == 1
        && host.Suspension().Fields[0].Remaining == 2, "suspension left field active or lost remaining lifetime");
    host.Skip(1000); Advance(host, 3);
    Check(!host.Resume(), "field capsule failed restoration");
    Advance(host, 2);
    Check(host.OwnedCount() == 0, "field lifetime did not expire on active boundaries");
    Advance(host, 2);
    Check(host.Trace().back().Result == Outcome::Rejected, "update revived an expired handle");
    host.Cancel(); host.Cancel();
    Check(host.CleanupCount() == 1 && host.Inventory == 1 && host.PendingCount() == 0,
        "repeated cleanup affected one-shot facts or leaked notifications");

    FakeHost fault(Start(Program(R"(process 1;
        emit field(cells(1), acceleration(2), acceleration(0), ticks(50)); wait ticks(1);
        emit update_field(effect_handle(), dig(cells(1)));
    )")));
    Advance(fault);
    Check(fault.Advance().Fault.has_value() && fault.OwnedCount() == 0, "fault did not remove previously owned field");
}
void CachedHandleReceipt() {
    const auto program = Program(R"(process 1;
        emit field(cells(1), acceleration(2), acceleration(0), ticks(20));
        wait ticks(2); await result within ticks(2); let h = effect_handle();
        wait ticks(1); emit stop_field(h); wait ticks(1);
    )");
    FakeHost host(Start(program));
    Advance(host, 2); host.Suspend();
    const auto &snapshot = host.Suspension().Runtime;
    Check(snapshot.Receipt && snapshot.Receipt->Handle.Id != 0, "early field receipt lost its token");
    auto invalid = snapshot;
    invalid.Receipt->Result = Outcome::Rejected;
    Check(std::holds_alternative<Diagnostic>(Instance::Restore(program, invalid)), "invalid cached field receipt accepted");
    host.Skip(1000);
    Check(!host.Resume(), "cached handle receipt failed restoration");
    Advance(host, 3);
    Check(host.OwnedCount() == 0 && host.Trace().size() == 2 && host.Trace().back().Result == Outcome::Applied,
        "cached receipt could not stop its original source after restore");
}
void CapacityAndIdentity() {
    FakeHost host(Start(Program(R"(process 1; var n = 0;
        while n < 33 {
            emit field(cells(1), acceleration(2), acceleration(0), ticks(100));
            wait ticks(1); n = n + 1;
        }
        wait ticks(20);
    )")));
    Advance(host, 33);
    Check(host.FieldSources().size() == 32 && host.Trace().back().Result == Outcome::Rejected,
        "fake host field capacity was not bounded");
    for (std::size_t i = 0; i < host.FieldSources().size(); ++i)
        Check(host.FieldSources()[i].Handle.Id == i + 1, "field identity allocation was not stable");
    host.Cancel();
    Check(host.OwnedCount() == 0, "termination left bounded fields active");
}
} // namespace
int main() {
    try {
        HandleRoundTrip(); TypesAndFaults(); AwaitResetAndValidation(); SnapshotValues();
        HostLifecycle(); CachedHandleReceipt(); CapacityAndIdentity();
        std::cout << "Process field source contracts passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
