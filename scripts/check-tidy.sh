#!/usr/bin/env bash

set -euo pipefail

readonly repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

cmake --preset quality
# Analysis imports require the same compiler-built BMIs as the real build.
cmake --build --preset quality --parallel 2

# Compile contracts deliberately contain invalid programs and have their own CTest runner.
run-clang-tidy-18 \
    -j 2 \
    -p "$repo_root/build/quality" \
    -quiet \
    -header-filter "^${repo_root}/(include|modules|src|tests|examples)/" \
    "^${repo_root}/(modules|src|tests|examples)/(?!compile_fail/).*\\.(c|cc|cpp|cppm|ixx|cxx)$"
