#!/usr/bin/env bash
# Configures and builds Debug and Release trees under build/.
# Usage: ci/build.sh [Debug|Release]...   (default: both)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGS=("$@")
[ ${#CONFIGS[@]} -eq 0 ] && CONFIGS=(Debug Release)
for cfg in "${CONFIGS[@]}"; do
    dir="$ROOT/build/${cfg,,}"
    cmake -S "$ROOT" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$cfg" ${QUILLQT_CMAKE_ARGS:-}
    cmake --build "$dir" --parallel
done
