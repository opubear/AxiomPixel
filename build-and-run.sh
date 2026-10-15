#!/usr/bin/env bash
set -euo pipefail

# Resolve paths relative to this script so it also works outside the project root.
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$project_dir/build"

cmake -S "$project_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DAXIOM_BUILD_RUNTIME=ON
cmake --build "$build_dir" --config Debug --target Pixel --parallel 4
exec cmake --build "$build_dir" --config Debug --target run-pixel
