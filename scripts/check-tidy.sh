#!/usr/bin/env bash

set -euo pipefail

readonly repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

cmake --preset quality

run-clang-tidy-18 \
    -p "$repo_root/build/quality" \
    -quiet \
    -header-filter "^${repo_root}/(include|src|tests|examples)/" \
    "^${repo_root}/(src|tests|examples)/.*\\.(c|cc|cpp|cxx)$"
