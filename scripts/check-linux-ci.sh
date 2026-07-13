#!/usr/bin/env bash

set -euo pipefail

readonly repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

cmake --workflow --preset linux-gcc-debug
cmake --workflow --preset linux-clang-coverage
bash ./scripts/check-quality.sh
