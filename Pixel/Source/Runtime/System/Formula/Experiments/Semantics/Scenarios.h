#pragma once
#include "Process.h"
#include "Formula/Simulator.h"

namespace pixel::formula::experiment {
enum class Scenario { Dig, Hit, Periodic, InventoryStop, Wake };
const char *Name(Scenario scenario);
struct Record {
    Tick At;
    Effect Value;
    Integer PositionX;
    bool operator==(const Record &) const = default;
};
struct Observation {
    std::vector<Record> Trace;
    std::size_t PeakPending = 0;
    unsigned Work = 0, Queries = 0, HostDecisions = 0;
};
struct Fixture {
    Scenario Kind;
    Formula Legacy;
    Program Hybrid;
    explicit Fixture(Scenario scenario);
};
Observation RunLegacy(const Fixture &fixture);
Observation RunHybrid(const Fixture &fixture);
void Verify();
void Benchmark();
} // namespace pixel::formula::experiment
