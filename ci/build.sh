#!/usr/bin/env bash
# Configures and builds trees under build/<config>.
# Usage: ci/build.sh [Debug|Release|Asan]...   (default: Debug Release)
#   Asan: Debug with AddressSanitizer and UndefinedBehaviorSanitizer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGS=("$@")
[ ${#CONFIGS[@]} -eq 0 ] && CONFIGS=(Debug Release)
for cfg in "${CONFIGS[@]}"; do
    dir="$ROOT/build/${cfg,,}"
    extra=()
    type="$cfg"
    if [ "$cfg" = "Asan" ]; then
        type=Debug
        extra=(-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
               -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined")
    fi
    cmake -S "$ROOT" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$type" "${extra[@]}" ${QUILLQT_CMAKE_ARGS:-}
    cmake --build "$dir" --parallel
done
