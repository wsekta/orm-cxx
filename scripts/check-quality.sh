#!/usr/bin/env bash

set -euo pipefail

readonly repo_root="$(cd "$(dirname "$0")/.." && pwd)"

bash "$repo_root/scripts/check-format.sh"
bash "$repo_root/scripts/check-tidy.sh"
