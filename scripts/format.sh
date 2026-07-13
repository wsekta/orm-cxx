#!/usr/bin/env bash

set -euo pipefail

readonly repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

mapfile -d '' -t candidates < <(
    git ls-files -z --cached --others --exclude-standard -- include src tests examples
)
cpp_files=()
for file in "${candidates[@]}"; do
    case "$file" in
        *.c | *.cc | *.cpp | *.cxx | *.h | *.hh | *.hpp | *.hxx)
            cpp_files+=("$file")
            ;;
    esac
done

clang-format-18 -i -- "${cpp_files[@]}"

mapfile -d '' -t cmake_files < <(
    git ls-files -z --cached --others --exclude-standard -- \
        CMakeLists.txt \
        ':(glob)**/CMakeLists.txt' \
        ':(glob)**/*.cmake' \
        ':(exclude)cmake/cmake-coverage.cmake' \
        ':(exclude,glob)cmake/third_party/**' \
        ':(exclude,glob)externals/**'
)

cmake-format -i "${cmake_files[@]}"
