#!/usr/bin/env bash
# Runs the test-suite of the built trees without a display.
# Usage: ci/test.sh [Debug|Release|Asan]...   (default: Debug Release)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGS=("$@")
[ ${#CONFIGS[@]} -eq 0 ] && CONFIGS=(Debug Release)
export QT_QPA_PLATFORM=offscreen
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-qt.qpa.*=false}"
# UBSan findings fail the run (the Asan build also stops on the first one).
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1:halt_on_error=1}"
for cfg in "${CONFIGS[@]}"; do
    ctest --test-dir "$ROOT/build/${cfg,,}" --output-on-failure
done
