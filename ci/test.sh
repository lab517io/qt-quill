#!/usr/bin/env bash
# Runs the test-suite of the built trees without a display.
# Usage: ci/test.sh [config]...   (default: Debug Release; configs as in ci/build.sh)
#   Mingw runs the Windows test binaries under Wine.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGS=("$@")
[ ${#CONFIGS[@]} -eq 0 ] && CONFIGS=(Debug Release)
# macOS ships bash 3.2: no ${var,,}.
lower() { echo "$1" | tr '[:upper:]' '[:lower:]'; }
export QT_QPA_PLATFORM=offscreen
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-qt.qpa.*=false}"
# UBSan findings fail the run (the Asan build also stops on the first one).
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
for cfg in "${CONFIGS[@]}"; do
    if [ "$cfg" = "Mingw" ]; then
        # shellcheck source=/dev/null
        source "$ROOT/ci/setup-mingw.sh"
    fi
    ctest --test-dir "$ROOT/build/$(lower "$cfg")" --output-on-failure ${QUILLQT_CTEST_ARGS:-}
done
