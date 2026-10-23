#pragma once
#include "FormulaTypes.h"
#include "ProcessCarrier.h"
#include <deque>
#include <memory>
#include <span>
#include <vector>

namespace pixel {
// Game host for process/1. The bridge evaluates every instance and sweeps every
// carrier before either language version commits world effects.
class ProcessHost {
  public:
    static constexpr size_t MaximumInstances = 16, MaximumProjectiles = 128;
    ProcessHost();
    ~ProcessHost();
    void BeginFrame();
    bool Start(const formula::process::ProgramArtifact &program, uint64_t id, uint64_t version,
               const ProcessCarrier &carrier, const World &world,
               std::optional<api::RequestKey> request, std::string &error);
    void Prepare(const World &world);
    void Commit(World &world, const std::optional<MiningCollector> &playerCollector = std::nullopt);
    void Clear();
    void CancelCarrier(ProcessCarrierKind kind, uint64_t subject);
    void WakeCarrier(ProcessCarrierKind kind, uint64_t subject);
    void AppendProjectiles(std::vector<FormulaProjectile> &output, uint64_t epoch) const;
    std::span<const FormulaEffectRecord> Effects() const;
    const std::deque<ProcessReceipt> &Receipts() const;
    const std::deque<FormulaRuntimeDiagnostic> &Diagnostics() const;
    ProcessHostStats Stats() const;
    std::vector<ProcessInspection> Inspect() const;

  private:
    struct State;
    std::unique_ptr<State> Data;
};
} // namespace pixel
