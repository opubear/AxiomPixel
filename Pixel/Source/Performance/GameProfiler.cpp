#include "GameProfiler.h"
#include "imgui.h"
#include <charconv>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#ifndef PIXEL_BUILD_TYPE
#define PIXEL_BUILD_TYPE "unknown"
#endif

namespace pixel::performance {
namespace {
size_t EnvironmentNumber(const char *name, size_t fallback, size_t minimum, size_t maximum) {
    const char *value = std::getenv(name);
    if (!value)
        return fallback;
    const std::string_view text(value);
    size_t parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || parsed < minimum || parsed > maximum)
        throw std::invalid_argument(std::string(name) + " is outside its supported integer range");
    return parsed;
}
} // namespace

std::string GameProfiler::ConfigureFromEnvironment() {
    Enabled = EnvironmentNumber("PIXEL_PROFILE", 1, 0, 1) != 0;
    CaptureFrames = EnvironmentNumber("PIXEL_PROFILE_FRAMES", 0, 1, 10000);
    Warmup = EnvironmentNumber("PIXEL_PROFILE_WARMUP", 30, 0, 10000);
    if (const char *path = std::getenv("PIXEL_PROFILE_CSV")) {
        OutputPath = path;
        if (OutputPath.empty())
            throw std::invalid_argument("PIXEL_PROFILE_CSV must be nonempty");
    }
    if (const char *scenario = std::getenv("PIXEL_PROFILE_SCENARIO"))
        Scenario = scenario;
    if (Scenario != "default" && Scenario != "no-wind-visual" && Scenario != "paused" &&
        Scenario != "paused-no-wind-visual" && Scenario != "field")
        throw std::invalid_argument("unknown PIXEL_PROFILE_SCENARIO");
    if (CaptureFrames && !Enabled)
        throw std::invalid_argument("automatic capture requires PIXEL_PROFILE=1");
    Collector = std::make_unique<FrameProfiler>(CaptureFrames ? CaptureFrames : 240);
    return Scenario;
}

void GameProfiler::BeginFrame() {
    if (ClearRequested) {
        Collector->Clear();
        Display = {};
        ClearRequested = false;
    }
    if (Enabled)
        Collector->BeginFrame();
    else
        Collector->CancelFrame();
}

bool GameProfiler::EndFrame() {
    if (!Collector->IsRecording())
        return false;
    Collector->EndFrame();
    ++Completed;
    if (CaptureFrames && Completed == Warmup)
        Collector->Clear();
    if (CaptureFrames && Completed == Warmup + CaptureFrames) {
        Save(OutputPath);
        WriteReport(std::cout);
        return true;
    }
    return false;
}

void GameProfiler::WriteReport(std::ostream &out) const {
    const auto summary = Collector->Summarize();
    out << "Pixel CPU frame profile: build=" << PIXEL_BUILD_TYPE << " scenario=" << Scenario
        << " frames=" << summary.Frames << " warmup=" << (CaptureFrames ? Warmup : 0) << '\n';
    out << "Wall clock CPU scopes; inclusive rows overlap. GPU execution is not measured.\n"
        << "frame = active + preceding gap (pacing/events/acquire/skip). untracked includes present/UI/input outside scopes.\n"
        << "stage,mean_ms,p95_ms,max_ms,self_ms_per_frame,calls_per_frame,ms_per_call\n" << std::fixed << std::setprecision(4);
    for (size_t i = 0; i < StageCount; ++i) {
        const auto &stage = summary.Stages[i];
        out << Name(Stage(i)) << ',' << stage.Milliseconds.Mean << ',' << stage.Milliseconds.P95 << ',' << stage.Milliseconds.Maximum
            << ',' << stage.SelfMilliseconds << ',' << stage.CallsPerFrame << ',' << stage.MillisecondsPerCall << '\n';
    }
    for (size_t i = 0; i < CounterCount; ++i)
        out << Name(Counter(i)) << "_per_frame=" << summary.Counters[i] << '\n';

    out << "Memory: frame-end retained CPU storage (sizeof + vector size/capacity). "
        << "Excludes allocator overhead, transient allocations, render snapshots, GPU and process RSS.\n"
        << "World excludes fields; fields = base_forces + wind; total = world + fields. "
        << "Peak is the maximum sampled value in retained history, not an allocation high-water mark.\n"
        << "component,samples,latest_used_bytes,latest_reserved_bytes,mean_used_bytes,mean_reserved_bytes,peak_reserved_bytes\n";
    for (size_t i = 0; i < MemoryComponentCount; ++i) {
        const auto &memory = summary.Memory[i];
        out << Name(MemoryComponent(i)) << ',' << memory.Samples;
        if (memory.Samples)
            out << ',' << memory.Latest.UsedBytes() << ',' << memory.Latest.ReservedBytes()
                << ',' << memory.MeanUsedBytes << ',' << memory.MeanReservedBytes << ',' << memory.PeakReservedBytes;
        else
            out << ",,,,,";
        out << '\n';
    }
}

void GameProfiler::Save(const std::string &path) {
    std::ofstream csv(path);
    if (!csv)
        throw std::runtime_error("cannot open performance CSV: " + path);
    Collector->WriteCsv(csv);
    csv.close();
    if (!csv)
        throw std::runtime_error("cannot finish performance CSV: " + path);
    std::ofstream report(path + ".summary.txt");
    WriteReport(report);
    report.close();
    if (!report)
        throw std::runtime_error("cannot finish performance summary: " + path);
    Status = "Saved " + path;
}

void GameProfiler::DrawUI() {
    if (!ImGui::CollapsingHeader("Performance"))
        return;
    if (!CaptureFrames) {
        if (ImGui::Checkbox("Collect CPU timings and memory", &Enabled)) {
            // Apply the toggle next frame; EndFrame still closes any active one.
            if (Enabled)
                ClearRequested = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear samples"))
            ClearRequested = true;
        if (ImGui::Button("Export performance CSV")) {
            try { Save(OutputPath); }
            catch (const std::exception &error) { Status = error.what(); }
        }
    }
    const auto now = FrameProfiler::Clock::now();
    if (now >= NextRefresh) {
        Display = Collector->Summarize();
        NextRefresh = now + std::chrono::milliseconds(500);
    }
    DrawMemoryUI();

    const auto &frame = Display.Stages[size_t(Stage::Frame)].Milliseconds;
    ImGui::Text("%zu frames: %.1f FPS; %.2f ms (p95 %.2f)", Display.Frames,
                frame.Mean > 0 ? 1000 / frame.Mean : 0, frame.Mean, frame.P95);
    ImGui::Text("Steps/frame: %.2f; uploads: %.1f MiB/frame",
        Display.Counters[size_t(Counter::SimulationSteps)],
        (Display.Counters[size_t(Counter::WorldUploadBytes)] + Display.Counters[size_t(Counter::WindUploadBytes)]) / (1024 * 1024));
    ImGui::TextDisabled("CPU wall time. Inclusive rows overlap; no GPU timestamps.");
    if (ImGui::BeginTable("Performance timings", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Stage"); ImGui::TableSetupColumn("Mean ms");
        ImGui::TableSetupColumn("P95 ms"); ImGui::TableSetupColumn("Self ms");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < StageCount; ++i) {
            const auto &stage = Display.Stages[i];
            if (!stage.CallsPerFrame)
                continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Name(Stage(i)).data());
            ImGui::TableNextColumn(); ImGui::Text("%.2f", stage.Milliseconds.Mean);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", stage.Milliseconds.P95);
            ImGui::TableNextColumn(); ImGui::Text("%.2f", stage.SelfMilliseconds);
        }
        ImGui::EndTable();
    }
    ImGui::TextWrapped("Gap includes pacing, events, acquire and skipped frames. Untracked includes present and other uninstrumented CPU work.");
    if (!Status.empty())
        ImGui::TextWrapped("%s", Status.c_str());
}

void GameProfiler::DrawMemoryUI() const {
    ImGui::Separator();
    ImGui::TextUnformatted("Memory: retained CPU storage (MiB)");
    if (!Display.Memory[size_t(MemoryComponent::Total)].Samples) {
        ImGui::TextDisabled("No memory samples. Enable collection and draw a frame.");
        return;
    }

    constexpr double MiB = 1024 * 1024;
    constexpr auto labels = std::to_array<const char *>({
        "World (excluding fields)", "Force fields", "  Base forces", "  Wind backend", "Total (world + fields)"});
    static_assert(labels.size() == MemoryComponentCount);
    if (ImGui::BeginTable("Performance memory", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Component");
        ImGui::TableSetupColumn("Last used");
        ImGui::TableSetupColumn("Last reserved");
        ImGui::TableSetupColumn("Window peak");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < MemoryComponentCount; ++i) {
            const auto &memory = Display.Memory[i];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(labels[i]);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", memory.Latest.UsedBytes() / MiB);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", memory.Latest.ReservedBytes() / MiB);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", memory.PeakReservedBytes / MiB);
        }
        ImGui::EndTable();
    }

    ImGui::TextWrapped("Frame-end samples: objects + vector size/capacity. Reserved includes unused capacity. "
                       "Excludes temporary allocations, allocator overhead, render snapshots and GPU; not process RSS.");
    ImGui::Separator();
}

} // namespace pixel::performance
