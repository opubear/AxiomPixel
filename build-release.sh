#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$project_dir/build-release"

cmake -S "$project_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAXIOM_BUILD_RUNTIME=ON
cmake --build "$build_dir" --config Release --target Pixel --parallel 4
printf 'Release 构建完成。启动游戏：%s/run-release.sh\n' "$project_dir"
