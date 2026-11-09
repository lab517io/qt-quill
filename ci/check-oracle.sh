#!/usr/bin/env bash
# Regenerates the OT oracle outputs and fails when they differ from the
# committed ones (tests/fixtures/ot/): a drift means quill-delta (or
# dart_quill_delta) changed behaviour, or a case was edited without
# regenerating. Needs Node.js 18+; the Dart part runs when `dart` is on PATH
# (or set DART=/path/to/dart), and is skipped otherwise.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/tools/oracle"
npm ci --no-audit --no-fund
node generate.mjs --random 500 --seed 42
DART="${DART:-$(command -v dart || true)}"
if [ -n "$DART" ]; then
    (cd "$ROOT/tools/dart-oracle" && "$DART" pub get && "$DART" run bin/oracle.dart)
else
    echo "dart not found: dart_quill_delta results not regenerated"
fi
cd "$ROOT"
status=$(git status --porcelain -- tests/fixtures/ot)
if [ -n "$status" ]; then
    echo "OT oracle outputs differ from the committed ones:" >&2
    echo "$status" >&2
    git --no-pager diff --stat -- tests/fixtures/ot >&2
    exit 1
fi
echo "oracle outputs unchanged"
