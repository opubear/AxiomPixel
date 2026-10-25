#include "Runtime/Simulation/World/World.h"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// Deterministic static geometry. CSV includes setup projection separately from
// evolution; samples use the same world coordinates for both solver families.
int main(int argc, char **argv) {
    try {
        const uint32_t width = argc > 1 ? uint32_t(std::stoul(argv[1])) : 64;
        const uint32_t height = argc > 2 ? uint32_t(std::stoul(argv[2])) : 48;
        const uint32_t steps = argc > 3 ? uint32_t(std::stoul(argv[3])) : 3;
        if (width < 8 || height < 8 || width > 2048 || height > 2048 || steps > 10000)
            throw std::invalid_argument("usage: PixelWindBenchmark [width 8..2048] [height 8..2048] [steps 0..10000] [sample.csv]");
        std::ofstream samples;
        if (argc > 4) {
            samples.open(argv[4]);
            if (!samples)
                throw std::runtime_error("cannot open sample CSV");
            samples << "scene,x,y,solid,reference_x,reference_y,coarse_x,coarse_y\n";
        }
        std::cout << "scene,width,height,steps,setup_ms,step_mean_ms,pressure_iterations,rms_divergence,max_divergence,converged,blocked_face_flux,coarse_velocity_rmse\n";
        using Clock = std::chrono::steady_clock;
        for (const std::string scene : {"empty", "wall", "slit"}) {
            pixel::World geometry(width, height);
            geometry.Fill({pixel::CellTypes::Empty, {0, 0}});
            if (scene != "empty")
                for (uint32_t y = 0; y < height; ++y)
                    if (scene != "slit" || y != height / 2)
                        geometry.PlaceDirt(width / 2, y);
            pixel::PixelWindSettings settings;
            settings.Ambient = {8, 0};
            settings.GustSpeed = 0;
            settings.Relaxation = 0;
            settings.Viscosity = 0;
            pixel::PixelWindField reference;
            auto start = Clock::now();
            reference.Reset(width, height, settings);
            reference.SetObstacles(geometry.ReadState());
            const double setup = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            start = Clock::now();
            bool converged = reference.GetReport().Converged;
            uint32_t iterations = reference.GetReport().PressureIterations;
            for (uint32_t step = 0; step < steps; ++step) {
                reference.Step(1.f / 60);
                converged &= reference.GetReport().Converged;
                iterations += reference.GetReport().PressureIterations;
            }
            const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            pixel::WindSettings coarseSettings;
            coarseSettings.Ambient = settings.Ambient;
            coarseSettings.GustSpeed = 0;
            coarseSettings.Relaxation = 0;
            coarseSettings.Viscosity = 0;
            pixel::WindField coarse;
            coarse.Reset(width, height, coarseSettings);
            coarse.CoupleDirt(geometry.ReadState(), {}, 1.f / 60);
            for (uint32_t step = 0; step < steps; ++step) {
                coarse.Step(1.f / 60);
                coarse.CoupleDirt(geometry.ReadState(), {}, 1.f / 60);
            }
            double flux = 0, error = 0;
            size_t count = 0;
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x) {
                    const bool solid = reference.IsSolid(x, y);
                    const auto a = reference.Sample(x + .5f, y + .5f), b = coarse.Sample(x + .5f, y + .5f);
                    if (solid)
                        flux = std::max({flux, std::abs(reference.FaceX(x, y)), std::abs(reference.FaceX((x + 1) % width, y)), std::abs(reference.FaceY(x, y)), std::abs(reference.FaceY(x, (y + 1) % height))});
                    else {
                        error += std::pow(a.X - b.X, 2) + std::pow(a.Y - b.Y, 2);
                        ++count;
                    }
                    if (samples)
                        samples << scene << ',' << x << ',' << y << ',' << solid << ',' << a.X << ',' << a.Y << ',' << b.X << ',' << b.Y << '\n';
                }
            const auto report = reference.GetReport();
            std::cout << scene << ',' << width << ',' << height << ',' << steps << ',' << setup << ',' << elapsed / std::max(1u, steps) << ',' << iterations << ',' << report.RmsDivergence << ',' << report.MaxDivergence << ',' << converged << ',' << flux << ',' << std::sqrt(error / std::max(size_t(1), count)) << std::endl;
            if (!converged)
                return 2;
        }
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
