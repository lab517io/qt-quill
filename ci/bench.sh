#!/usr/bin/env bash
# Large-document timings (milestone M11): runs quill-bench from the Release
# tree and compares the results with tools/bench/budgets.json.
# Usage: ci/bench.sh [--full]
#   default: the corpora and scenarios the budgets need (a few minutes)
#   --full:  every corpus and scenario (about ten minutes)
# Environment: QUILLQT_BENCH_SLACK (default 3) multiplies absolute budgets;
# QUILLQT_BENCH_STRICT=1 makes exceeded budgets fail the script.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="$ROOT/build/release/tools/bench/quill-bench"
[ -x "$BENCH" ] || { echo "build first: ci/build.sh Release" >&2; exit 1; }
OUT="$ROOT/build/release/bench.json"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
args=()
if [ "${1:-}" != "--full" ]; then
    args=(--corpus mixed,plain,nested-lists,long-line
          --scenario load,qt-load,type-middle,qt-typing,update-1char,paste-undo,getContents,scroll,export)
fi
"$BENCH" ${args[@]+"${args[@]}"} --json "$OUT"
strict=()
[ "${QUILLQT_BENCH_STRICT:-0}" = 1 ] && strict=(--strict)
python3 "$ROOT/tools/bench/check.py" "$OUT" --slack "${QUILLQT_BENCH_SLACK:-3}" ${strict[@]+"${strict[@]}"}
