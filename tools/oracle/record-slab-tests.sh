#!/usr/bin/env bash
# Regenerates tests/fixtures/delta/slab-delta-recorded.json by running the
# slab/delta test-suite (pinned commit) with recorder-helper.cjs.
# Requires git, Node.js >= 18 and network access. The checkout lives in a
# temporary directory; nothing from it is copied except the recording.
set -euo pipefail
COMMIT="${DELTA_COMMIT:-dc17ca03e1d68ee729c8cd1ff790ebf96eb0fbec}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$ROOT/tests/fixtures/delta/slab-delta-recorded.json"
WORK="${WORK_DIR:-$(mktemp -d)}"

git -C "$WORK" init -q delta
git -C "$WORK/delta" fetch -q --depth 1 https://github.com/slab/delta "$COMMIT"
git -C "$WORK/delta" checkout -q FETCH_HEAD
(cd "$WORK/delta" && npm ci --silent --ignore-scripts)

cat > "$WORK/jasmine.json" <<JSON
{
  "spec_dir": ".",
  "spec_files": ["test/**/*.ts"],
  "helpers": ["jasmine-typescript-helper.js", "$HERE/recorder-helper.cjs"],
  "random": false,
  "jsLoader": "require"
}
JSON
mkdir -p "$(dirname "$OUT")"
(cd "$WORK/delta" && DELTA_DIR="$WORK/delta" DELTA_COMMIT="$COMMIT" RECORD_OUTPUT="$OUT" \
    npx jasmine --config="$WORK/jasmine.json")
echo "wrote $OUT"
