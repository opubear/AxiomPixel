#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <vector>

// A disposable M3.1 experiment, deliberately outside the production API.
namespace pixel::formula::experiment {
using Integer = std::int64_t;
using Tick = std::uint64_t;
struct Site {
    std::string File;
    unsigned Line;
};

enum class ExpressionKind { Constant, Local, Inventory, Add, Less };
struct Expression;
using Expr = std::shared_ptr<const Expression>;
struct Expression {
    ExpressionKind Kind;
    Integer Value = 0;
    Expr Left, Right;
};
Expr Constant(Integer value);
Expr Local(unsigned slot);
Expr Inventory();
Expr Add(Expr left, Expr right);
Expr Less(Expr left, Expr right);

enum class EffectKind { Dig, Wind, Bolt };
// Distinct meanings: radius in cells; wind delta velocity in cells/second.
// Bolt is a fixed test carrier (speed 60, damage 10, lifetime 20).
struct Effect {
    EffectKind Kind;
    Integer RadiusCells = 0;
    Integer DeltaVelocity = 0;
    bool operator==(const Effect &) const = default;
};
enum class EventKind { Hit, Wake, Applied, Rejected };
enum class StatementKind { Assign, Emit, While, If, Wait, Await, Stop };
struct Statement {
    StatementKind Kind;
    Site Source;
    unsigned Slot = 0;
    Expr Value;
    Effect Output{EffectKind::Dig};
    Tick Delay = 0;
    EventKind Event = EventKind::Wake;
    std::vector<Statement> Body;
};
using Block = std::vector<Statement>;
Statement Assign(unsigned slot, Expr value, std::source_location site = std::source_location::current());
Statement Emit(Effect effect, std::source_location site = std::source_location::current());
Statement While(Expr condition, Block body, std::source_location site = std::source_location::current());
Statement If(Expr condition, Block body, std::source_location site = std::source_location::current());
Statement Wait(Tick ticks, std::source_location site = std::source_location::current());
Statement Await(EventKind event, std::source_location site = std::source_location::current());
Statement Stop(std::source_location site = std::source_location::current());

enum class Opcode { Assign, Emit, BranchFalse, Jump, Wait, Await, Stop };
struct Instruction {
    Opcode Op;
    Statement Data;
    std::size_t Target = 0;
};
class Program {
  public:
    explicit Program(const Block &source, std::source_location site = std::source_location::current());
    const std::vector<Instruction> &Code() const { return Instructions; }
  private:
    std::vector<Instruction> Instructions;
};

enum class Phase { Ready, Sleeping, Listening, Done, Trapped };
struct State {
    std::size_t PC = 0;
    std::array<Integer, 4> Locals{};
    Tick ActiveTicks = 0, Remaining = 0;
    std::uint64_t NextEmission = 1, AwaitToken = 0, EventFloor = 0;
    EventKind WaitingFor = EventKind::Wake;
    Phase Status = Phase::Ready;
    Integer OriginX = 0;
};
struct View {
    Tick CommittedTick;
    Integer InventoryCount;
    std::uint64_t EventHighWatermark = 0;
};
struct HostEvent {
    std::uint64_t Sequence;
    Tick CommittedTick;
    EventKind Kind;
    std::uint64_t Token;
    Integer PositionX = 0;
};
struct Intent {
    std::uint64_t Token;
    Effect Value;
    Integer PositionX;
};
struct Limits {
    unsigned Work = 64, Queries = 8, Effects = 16;
};
struct Slice {
    std::vector<Intent> Effects;
    unsigned Work = 0, Queries = 0;
    std::string Trap;
    Site Location;
};
// The caller supplies one frozen view and a sorted, <=16 event batch per active
// tick. It commits all instances' successful slices only after evaluation ends.
// Skipping this call freezes local time; no wall-clock catch-up is performed.
Slice Step(const Program &program, State &state, const View &view,
           std::span<const HostEvent> events = {}, Limits limits = {});
} // namespace pixel::formula::experiment
