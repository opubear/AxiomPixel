#include "Runtime/Simulation/World/World.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char **argv) {
    try {
        const int size = argc > 1 ? std::stoi(argv[1]) : 64;
        const int steps = argc > 2 ? std::stoi(argv[2]) : 4;
        const bool reference = argc <= 3 || std::string(argv[3]) != "skip-reference";
        if (size < 8 || size > 2048 || steps < 0 || steps > 10000)
            throw std::invalid_argument("usage: benchmark [size 8..2048] [steps 0..10000] [skip-reference]");
        using Clock = std::chrono::steady_clock;
        std::cout << "scenario,size,strategy,init_ms,step_mean_ms,air_speed,divergence_rms,mass,stabilizations\n";
        for (int scene = 0; scene < 3; ++scene) {
            pixel::World world(size, size / 2);
            if (scene)
                for (int y = 0; y < size / 2; ++y)
                    if (scene != 2 || y != size / 4)
                        world.PlaceDirt(size / 2, y);
            for (int kind = 0; kind < 6; ++kind) {
                if (kind == 1 && !reference)
                    continue;
                pixel::WindStrategySettings s;
                s.Kind = pixel::WindStrategyKind(kind);
                s.Coarse.Ambient = s.Reference.Ambient = s.Adaptive.Flow.Ambient = s.Lbm.Ambient = {8, 0};
                s.Coarse.GustSpeed = s.Reference.GustSpeed = s.Adaptive.Flow.GustSpeed = s.Lbm.GustSpeed = 0;
                s.Coarse.Relaxation = s.Reference.Relaxation = s.Adaptive.Flow.Relaxation = s.Lbm.Relaxation = 0;
                const auto start = Clock::now();
                auto strategy = pixel::MakeWindStrategy(world.ReadState(), size / 2, s);
                const auto initialized = Clock::now();
                for (int step = 0; step < steps; ++step) {
                    strategy->AdvanceBeforeMaterials(world.ReadState(), 1.f / 60);
                    strategy->UpdateAfterMaterials(world.ReadState(), {}, 1.f / 60);
                }
                const auto done = Clock::now();
                const auto sample = strategy->Sample(size / 2 + 1.5f, size / 4 + .5f);
                double divergence = -1, mass = -1;
                uint64_t stabilizations = 0;
                if (const auto *r = strategy->ReferenceDiagnostics())
                    divergence = r->GetReport().RmsDivergence;
                if (const auto *r = strategy->AdaptiveDiagnostics())
                    divergence = r->GetReport().RmsDivergence;
                if (const auto *r = strategy->CoarseDiagnostics())
                    divergence = r->RmsDivergence();
                if (const auto *r = strategy->LbmDiagnostics()) {
                    mass = r->GetReport().Mass;
                    stabilizations = r->GetReport().Stabilizations;
                }
                std::cout << (scene == 0 ? "empty" : scene == 1 ? "wall"
                                                                : "slit")
                          << ',' << size << ',' << kind << ','
                          << std::chrono::duration<double, std::milli>(initialized - start).count() << ','
                          << (steps ? std::chrono::duration<double, std::milli>(done - initialized).count() / steps : 0) << ','
                          << std::hypot(sample.X, sample.Y) << ',' << divergence << ',' << mass << ',' << stabilizations << '\n';
            }
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
