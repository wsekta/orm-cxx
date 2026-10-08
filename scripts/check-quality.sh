#!/usr/bin/env bash

set -euo pipefail

readonly repo_root="$(cd "$(dirname "$0")/.." && pwd)"

python3 -m unittest discover -s "$repo_root/tests/macros" -v
python3 "$repo_root/scripts/check-macros.py"
bash "$repo_root/scripts/check-format.sh"
bash "$repo_root/scripts/check-tidy.sh"
