#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
game_executable="$project_dir/build/bin/Debug/Pixel"

if [[ ! -x "$game_executable" ]]; then
    printf '找不到可执行的游戏程序：%s\n请先运行项目根目录的 build-and-run.sh 完成编译。\n' "$game_executable" >&2
    exit 1
fi

cd "$project_dir/Pixel"
exec "$game_executable" "$@"
