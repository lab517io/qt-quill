#!/usr/bin/env bash
# Runs the test-suite of the built trees without a display.
# Usage: ci/test.sh [Debug|Release]...   (default: both)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIGS=("$@")
[ ${#CONFIGS[@]} -eq 0 ] && CONFIGS=(Debug Release)
export QT_QPA_PLATFORM=offscreen
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-qt.qpa.*=false}"
for cfg in "${CONFIGS[@]}"; do
    ctest --test-dir "$ROOT/build/${cfg,,}" --output-on-failure
done
