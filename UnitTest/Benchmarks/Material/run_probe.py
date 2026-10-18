#!/usr/bin/env python3
"""Build/run isolated copies of the production material step for investigation.

No production source is patched. Cached destinations are only valid for these
fixed-force, Empty-wind scenes; invalidation is intentionally not implemented.
"""

import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "Pixel/Source"
ENVIRONMENT = SOURCE / "World/Environment/Environment.cpp"


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError(f"Production source changed; inspect probe transformation: {old}")
    return text.replace(old, new, 1)


def prepare(output):
    generated = output / "source"
    generated.mkdir(parents=True, exist_ok=True)
    original = ENVIRONMENT.read_text()
    direct = replace_once(
        original,
        "world.GetFieldVelocity(uint32_t(source % state.Width), uint32_t(source / state.Width))",
        "FieldResponse::MovementVelocity(world.GetBaseForceField().Get(uint32_t(source % state.Width), uint32_t(source / state.Width)))",
    )
    start = original.index("    const auto velocity =", original.index("std::optional<size_t> ForceDestination"))
    end = original.index("\n}", start)
    cached = '#include "ProbeMetrics.h"\n' + original[:start] + """    const auto destination = probe::Destinations[source];
    if (destination == probe::NoDestination)
        return std::nullopt;
    return size_t(destination);""" + original[end:]
    counted = '#include "ProbeMetrics.h"\n' + original
    counted = replace_once(counted, "    const auto velocity =", "    ++probe::Samples;\n    const auto velocity =")
    counted = replace_once(counted, "    MovementChain.clear();", "    ++probe::Chains;\n    MovementChain.clear();")
    counted = replace_once(counted, "        MovementChain.push_back({source, *destination});", """        MovementChain.push_back({source, *destination});
        ++probe::Links;
        probe::MaxChain = std::max(probe::MaxChain, MovementChain.size());
        if (MovementChain.capacity() != probe::Capacity) {
            ++probe::Growths;
            probe::Capacity = MovementChain.capacity();
        }""")
    counted = replace_once(counted, "        world.MoveMaterial(move->Source, move->Destination);", "        ++probe::Moves;\n        world.MoveMaterial(move->Source, move->Destination);")
    for name, content in {"baseline": original, "direct": direct, "cached": cached, "counters": counted}.items():
        (generated / f"{name}.cpp").write_text(content)
    (generated / "FieldMain.cpp").write_text("void RunPixelFieldTests();\nint main() { RunPixelFieldTests(); }\n")
    common = sorted(path for path in (SOURCE / "World").rglob("*.cpp") if path != ENVIRONMENT)
    common.append(SOURCE / "Performance/FrameProfiler.cpp")
    sources = "\n".join(f'    "{path}"' for path in common)
    cmake = f"""cmake_minimum_required(VERSION 3.23)
project(MaterialInvestigation LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include_directories("{SOURCE}" "{SOURCE / 'World/Environment'}" "{HERE}")
add_library(common STATIC
{sources})
foreach(variant baseline direct cached counters)
    add_executable(${{variant}} "{HERE / 'MaterialProbe.cpp'}" ${{variant}}.cpp)
    target_link_libraries(${{variant}} PRIVATE common)
endforeach()
add_executable(field_contracts FieldMain.cpp "{ROOT / 'UnitTest/World/PixelFieldTests.cpp'}" baseline.cpp)
target_link_libraries(field_contracts PRIVATE common)
add_executable(baseline_contracts "{ROOT / 'UnitTest/World/PixelBaselineTests.cpp'}" baseline.cpp)
target_link_libraries(baseline_contracts PRIVATE common)
add_executable(wind_contracts "{ROOT / 'UnitTest/World/PixelWindStrategyTests.cpp'}" baseline.cpp)
target_link_libraries(wind_contracts PRIVATE common)
"""
    (generated / "CMakeLists.txt").write_text(cmake)
    manifest = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(SOURCE.rglob("*")) if path.is_file() and path.suffix in {".cpp", ".h"}}
    (output / "source-hashes.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return generated


def summarize(output):
    rows = []
    hashes = {}
    for path in sorted(output.glob("*-timing.csv")):
        config, variant, *_ = path.stem.split("-")
        groups = {}
        with path.open() as source:
            for row in csv.DictReader(source):
                groups.setdefault(row["scene"], []).append(row)
                key = (row["scene"], row["round"])
                if key in hashes and hashes[key] != row["hash"]:
                    raise RuntimeError(f"State mismatch: {path} {key}")
                hashes[key] = row["hash"]
        for scene, samples in groups.items():
            values = sorted(float(row["material_ms"]) for row in samples)
            rows.append([config, variant, scene, len(values), statistics.mean(values),
                         statistics.median(values), values[int(len(values) * .95) - 1],
                         statistics.mean(float(row["cache_build_ms"]) for row in samples)])
    with (output / "summary.csv").open("w") as destination:
        writer = csv.writer(destination)
        writer.writerow(["build", "variant", "scene", "samples", "mean_ms", "median_ms", "p95_ms", "cache_build_ms"])
        writer.writerows(rows)
    print(f"Verified matching final world hashes across {len(hashes)} scene/round pairs; wrote {output / 'summary.csv'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=["build", "run", "contracts", "summarize"])
    parser.add_argument("--output", type=Path, default=ROOT / "build-profile-results/2026-10-16-material")
    parser.add_argument("--config", choices=["Debug", "RelWithDebInfo"], default="RelWithDebInfo")
    parser.add_argument("--variant", choices=["baseline", "direct", "cached", "counters"], default="baseline")
    parser.add_argument("--scene", default="", help="Only run this named scene; save it in a separate CSV")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    build = output / args.config
    if args.action == "build":
        source = prepare(output)
        subprocess.run(["cmake", "-S", str(source), "-B", str(build), f"-DCMAKE_BUILD_TYPE={args.config}"], check=True)
        subprocess.run(["cmake", "--build", str(build), "-j4"], check=True)
    elif args.action == "run":
        mode = "counts" if args.variant == "counters" else "cached" if args.variant == "cached" else "timing"
        suffix = "counts" if mode == "counts" else "timing"
        scope = f"-{args.scene}" if args.scene else ""
        destination = output / f"{args.config}-{args.variant}{scope}-{suffix}.csv"
        with destination.open("w") as stream:
            subprocess.run([str(build / args.variant), mode, args.scene], stdout=stream, check=True)
        print(destination)
        if args.variant == "baseline":
            subprocess.run([str(build / "baseline"), "semantics"], check=True)
    elif args.action == "contracts":
        for target in ["field_contracts", "baseline_contracts", "wind_contracts"]:
            subprocess.run([str(build / target)], check=True)
            print(f"PASS {args.config} {target}", flush=True)
    else:
        summarize(output)


if __name__ == "__main__":
    main()
