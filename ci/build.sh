#!/usr/bin/env bash
# Configures and builds trees under build/<config, lower case>.
# Usage: ci/build.sh [config]...   (default: Debug Release)
#   Debug, Release   system Qt and compiler
#   Asan             Debug with AddressSanitizer and UndefinedBehaviorSanitizer
#   Clang            Debug with clang++
#   Qt-<version>     Debug against another Qt, e.g. Qt-6.8.3: installed by
#                    ci/install-qt.sh, or found in ~/Qt/<version>/gcc_64
#   Mingw            Windows cross-build with llvm-mingw (see ci/setup-mingw.sh)
# QUILLQT_WERROR=ON makes warnings errors; QUILLQT_CMAKE_ARGS adds arguments.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGS=("$@")
[ ${#CONFIGS[@]} -eq 0 ] && CONFIGS=(Debug Release)
# macOS ships bash 3.2: no ${var,,}.
lower() { echo "$1" | tr '[:upper:]' '[:lower:]'; }

qt_prefix() {
    local version="$1" qtroot="${QUILLQT_QT_ROOT:-$HOME/.cache/quillqt-qt}"
    for p in "$qtroot/$version/gcc_64" "$HOME/Qt/$version/gcc_64"; do
        [ -f "$p/lib/cmake/Qt6/Qt6Config.cmake" ] && { echo "$p"; return; }
    done
    "$ROOT/ci/install-qt.sh" "$version"
}

for cfg in "${CONFIGS[@]}"; do
    dir="$ROOT/build/$(lower "$cfg")"
    type="$cfg"
    extra=()
    case "$cfg" in
        Debug|Release) ;;
        Asan)
            type=Debug
            extra=(-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined"
                   -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined") ;;
        Clang)
            # -Wshorten-64-to-32 stands in for MSVC's C4244 (qsizetype -> int).
            type=Debug
            extra=(-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-Wshorten-64-to-32) ;;
        Qt-*)
            type=Debug
            prefix="$(qt_prefix "${cfg#Qt-}")"
            extra=(-DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_BUILD_RPATH="$prefix/lib") ;;
        Mingw)
            type=Debug
            # shellcheck source=/dev/null
            source "$ROOT/ci/setup-mingw.sh"
            extra=(-DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/llvm-mingw.cmake"
                   -DCMAKE_PREFIX_PATH="$QT_WINDOWS" -DQT_HOST_PATH="$QT_HOST") ;;
        *) echo "unknown config $cfg" >&2; exit 1 ;;
    esac
    cmake -S "$ROOT" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$type" -DQUILLQT_WERROR="${QUILLQT_WERROR:-OFF}" \
        ${extra[@]+"${extra[@]}"} ${QUILLQT_CMAKE_ARGS:-}
    cmake --build "$dir" --parallel
done
