#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
engine_dir="$project_dir/Axiom"
branch=PixelMain

if [[ "$engine_dir" -ef "$project_dir/../Axiom" ]]; then
    printf 'Axiom/ points to the read-only sibling repository. Use a separate checkout for this project.\n' >&2
    exit 1
fi

if [[ -e "$engine_dir/.git" ]]; then
    current_branch="$(git -C "$engine_dir" branch --show-current)"
    if [[ "$current_branch" != "$branch" ]]; then
        printf 'Axiom/ is on %s. To sync %s, run git -C Axiom switch %s first.\n' \
            "${current_branch:-detached HEAD}" "$branch" "$branch" >&2
        exit 1
    fi
    git -C "$engine_dir" pull --ff-only --no-recurse-submodules origin "$branch"
else
    git clone --branch "$branch" git@github.com:opubear/Axiom.git "$engine_dir"
fi

git -C "$engine_dir" submodule sync --recursive
git -C "$engine_dir" submodule update --init --recursive
