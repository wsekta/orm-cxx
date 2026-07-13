#!/usr/bin/env bash

set -euo pipefail

readonly repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

cmake --workflow --preset linux-clang-debug
bash ./scripts/check-quality.sh
