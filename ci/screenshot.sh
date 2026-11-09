#!/usr/bin/env bash
# Renders quill-demo with every embedded sample, light and dark, into
# build/<config>/screenshots/. Uses xvfb-run when available, otherwise Qt's
# offscreen platform.
# Usage: ci/screenshot.sh [debug|release]   (default: debug)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CFG="${1:-debug}"
DEMO="$ROOT/build/$CFG/apps/demo/quill-demo"
OUT="$ROOT/build/$CFG/screenshots"
[ -x "$DEMO" ] || { echo "build first: $DEMO missing" >&2; exit 1; }
mkdir -p "$OUT"

run() {
    if command -v xvfb-run >/dev/null 2>&1; then
        xvfb-run -a -s "-screen 0 1600x1000x24" "$@"
    else
        QT_QPA_PLATFORM=offscreen "$@"
    fi
}

samples=$(sed -n 's/.*alias="\([^"]*\)".*/\1/p' "$ROOT/apps/demo/samples.qrc")
for s in $samples; do
    name="${s%.json}"
    run "$DEMO" --offline --sample "$s" --screenshot "$OUT/$name.png"
    run "$DEMO" --offline --sample "$s" --dark --screenshot "$OUT/$name-dark.png"
done
echo "screenshots in $OUT"
