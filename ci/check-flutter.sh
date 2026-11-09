#!/usr/bin/env bash
# Checks quill-qt against flutter_quill (tools/flutter-check): every fixture
# and every document exported by quill-qt loads in flutter_quill without
# loss and renders, and flutter_quill's own exports and editing scripts
# (tests/fixtures/flutter_quill/generated, scripts.json) are unchanged.
# Needs Flutter (set FLUTTER=/path/to/flutter if it is not on PATH).
# With WRITE_FIXTURES=1 the flutter_quill fixtures are rewritten instead.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FLUTTER="${FLUTTER:-$(command -v flutter || true)}"
[ -n "$FLUTTER" ] || { echo "flutter not found (set FLUTTER)" >&2; exit 1; }
cd "$ROOT/tools/flutter-check"
"$FLUTTER" pub get
"$FLUTTER" test
