#include "Process.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace pixel::formula::experiment {
namespace {
Site At(std::source_location site) { return {site.file_name(), site.line()}; }
Statement Make(StatementKind kind, std::source_location site) { return {kind, At(site)}; }

void ValidateExpression(const Expr &expression, unsigned depth = 0) {
    if (!expression || depth > 16)
        throw std::invalid_argument("experiment expression missing or too deep");
    if (expression->Kind == ExpressionKind::Local && (expression->Value < 0 || expression->Value >= 4))
        throw std::invalid_argument("experiment local slot out of range");
    if (expression->Kind == ExpressionKind::Add || expression->Kind == ExpressionKind::Less) {
        ValidateExpression(expression->Left, depth + 1);
        ValidateExpression(expression->Right, depth + 1);
    }
}

void Lower(const Block &body, std::vector<Instruction> &code, unsigned depth = 0) {
    if (depth > 16)
        throw std::invalid_argument("experiment block too deep");
    for (const auto &statement : body) {
        if (code.size() >= 256)
            throw std::invalid_argument("experiment program too large");
        const auto start = code.size();
        Statement flat{statement.Kind, statement.Source, statement.Slot, statement.Value,
                       statement.Output, statement.Delay, statement.Event, {}};
        switch (statement.Kind) {
        case StatementKind::Assign:
            if (statement.Slot >= 4) throw std::invalid_argument("experiment local slot out of range");
            ValidateExpression(statement.Value);
            code.push_back({Opcode::Assign, flat});
            break;
        case StatementKind::Emit:
            if (statement.Output.RadiusCells < 0)
                throw std::invalid_argument("experiment negative radius");
            code.push_back({Opcode::Emit, flat});
            break;
        case StatementKind::If:
        case StatementKind::While:
            ValidateExpression(statement.Value);
            code.push_back({Opcode::BranchFalse, flat});
            Lower(statement.Body, code, depth + 1);
            if (statement.Kind == StatementKind::While) code.push_back({Opcode::Jump, flat, start});
            code[start].Target = code.size();
            break;
        case StatementKind::Wait:
            if (statement.Delay == 0) throw std::invalid_argument("wait must advance active time");
            code.push_back({Opcode::Wait, flat});
            break;
        case StatementKind::Await: code.push_back({Opcode::Await, flat}); break;
        case StatementKind::Stop: code.push_back({Opcode::Stop, flat}); break;
        }
    }
}

// Only these local language traps are caught. Broken host/compiler contracts
// remain exceptions; a runtime bug must not become a successful empty slice.
struct Trap { const char *Reason; };
struct Evaluation {
    const View &Snapshot;
    const State &Instance;
    Limits Budget;
    Slice &Result;

    void Charge() {
        if (Result.Work == Budget.Work) throw Trap{"work limit"};
        ++Result.Work;
    }

    Integer Read(const Expr &expression) {
        Charge();
        switch (expression->Kind) {
        case ExpressionKind::Constant: return expression->Value;
        case ExpressionKind::Local: return Instance.Locals.at(static_cast<std::size_t>(expression->Value));
        case ExpressionKind::Inventory:
            if (Result.Queries == Budget.Queries) throw Trap{"query limit"};
            ++Result.Queries;
            return Snapshot.InventoryCount;
        case ExpressionKind::Add: {
            const auto left = Read(expression->Left);
            const auto right = Read(expression->Right);
            if ((right > 0 && left > std::numeric_limits<Integer>::max() - right)
                || (right < 0 && left < std::numeric_limits<Integer>::min() - right))
                throw Trap{"integer overflow"};
            return left + right;
        }
        case ExpressionKind::Less: {
            const auto left = Read(expression->Left);
            const auto right = Read(expression->Right);
            return left < right;
        }
        }
        throw std::logic_error("unknown expression opcode");
    }
};

bool Wake(State &state, const View &view, std::span<const HostEvent> events) {
    if (state.Status == Phase::Sleeping) {
        if (--state.Remaining != 0) return false;
        state.Status = Phase::Ready;
    }
    if (state.Status == Phase::Listening) {
        for (const auto &event : events) {
            if (event.Sequence <= state.EventFloor || event.CommittedTick >= view.CommittedTick
                || event.Kind != state.WaitingFor || event.Token != state.AwaitToken) continue;
            state.EventFloor = event.Sequence;
            if (event.Kind == EventKind::Hit) state.OriginX = event.PositionX;
            state.Status = Phase::Ready;
            break;
        }
    }
    return state.Status == Phase::Ready;
}
} // namespace

Expr Constant(Integer value) { return std::make_shared<Expression>(Expression{ExpressionKind::Constant, value}); }
Expr Local(unsigned slot) { return std::make_shared<Expression>(Expression{ExpressionKind::Local, slot}); }
Expr Inventory() { return std::make_shared<Expression>(Expression{ExpressionKind::Inventory}); }
Expr Add(Expr left, Expr right) { return std::make_shared<Expression>(Expression{ExpressionKind::Add, 0, left, right}); }
Expr Less(Expr left, Expr right) { return std::make_shared<Expression>(Expression{ExpressionKind::Less, 0, left, right}); }

Statement Assign(unsigned slot, Expr value, std::source_location site) {
    auto result = Make(StatementKind::Assign, site);
    result.Slot = slot;
    result.Value = std::move(value);
    return result;
}
Statement Emit(Effect effect, std::source_location site) {
    auto result = Make(StatementKind::Emit, site);
    result.Output = effect;
    return result;
}
Statement While(Expr condition, Block body, std::source_location site) {
    auto result = Make(StatementKind::While, site);
    result.Value = std::move(condition);
    result.Body = std::move(body);
    return result;
}
Statement If(Expr condition, Block body, std::source_location site) {
    auto result = While(std::move(condition), std::move(body), site);
    result.Kind = StatementKind::If;
    return result;
}
Statement Wait(Tick ticks, std::source_location site) {
    auto result = Make(StatementKind::Wait, site);
    result.Delay = ticks;
    return result;
}
Statement Await(EventKind event, std::source_location site) {
    auto result = Make(StatementKind::Await, site);
    result.Event = event;
    return result;
}
Statement Stop(std::source_location site) { return Make(StatementKind::Stop, site); }

Program::Program(const Block &source, std::source_location site) {
    Lower(source, Instructions);
    auto end = Stop(site);
    if (!source.empty()) end.Source = source.back().Source;
    Instructions.push_back({Opcode::Stop, end});
    if (Instructions.size() > 256) throw std::invalid_argument("experiment program too large");
}

Slice Step(const Program &program, State &state, const View &view, std::span<const HostEvent> events, Limits limits) {
    Slice result;
    if (events.size() > 16) throw std::logic_error("host event batch exceeds experiment contract");
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].Sequence > view.EventHighWatermark
            || (i && events[i - 1].Sequence >= events[i].Sequence))
            throw std::logic_error("host events must be ordered within the frozen watermark");
    }
    if (state.Status == Phase::Done || state.Status == Phase::Trapped) return result;
    if (state.ActiveTicks == std::numeric_limits<Tick>::max()) throw std::overflow_error("active clock overflow");
    ++state.ActiveTicks;
    if (!Wake(state, view, events)) return result;

    Evaluation evaluation{view, state, limits, result};
    try {
        while (state.Status == Phase::Ready) {
            const auto &instruction = program.Code().at(state.PC);
            result.Location = instruction.Data.Source;
            evaluation.Charge();
            ++state.PC;
            switch (instruction.Op) {
            case Opcode::Assign: state.Locals.at(instruction.Data.Slot) = evaluation.Read(instruction.Data.Value); break;
            case Opcode::BranchFalse:
                if (!evaluation.Read(instruction.Data.Value)) state.PC = instruction.Target;
                break;
            case Opcode::Jump: state.PC = instruction.Target; break;
            case Opcode::Emit:
                if (result.Effects.size() == limits.Effects) throw Trap{"effect limit"};
                if (state.NextEmission == std::numeric_limits<std::uint64_t>::max()) throw Trap{"emission overflow"};
                result.Effects.push_back({state.NextEmission++, instruction.Data.Output, state.OriginX});
                break;
            case Opcode::Wait:
                state.Remaining = instruction.Data.Delay;
                state.Status = Phase::Sleeping;
                break;
            case Opcode::Await:
                state.WaitingFor = instruction.Data.Event;
                state.AwaitToken = state.WaitingFor == EventKind::Wake ? 0 : state.NextEmission - 1;
                if (state.WaitingFor != EventKind::Wake && state.AwaitToken == 0)
                    throw Trap{"await requires an emitted request"};
                state.EventFloor = view.EventHighWatermark;
                state.Status = Phase::Listening;
                break;
            case Opcode::Stop: state.Status = Phase::Done; break;
            }
        }
    } catch (const Trap &trap) {
        result.Trap = trap.Reason;
        result.Effects.clear();
        state.Status = Phase::Trapped;
    }
    return result;
}
} // namespace pixel::formula::experiment
