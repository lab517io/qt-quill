#!/usr/bin/env bash
# Interoperability check of the quill-collab wire format: starts the C++
# server, lets three quill-delta (JavaScript) clients edit concurrently,
# then checks that they and the server's document are identical.
# Needs Node.js 18+ and a build of quill-collab.
# Usage: ci/check-collab-node.sh [debug|release]   (default: debug)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CFG="${1:-debug}"
BIN="$ROOT/build/$CFG/apps/collab/quill-collab"
[ -x "$BIN" ] || { echo "build first: $BIN missing" >&2; exit 1; }
cd "$ROOT/tools/collab-node"
[ -d node_modules/quill-delta ] || npm ci --no-audit --no-fund

OUT="$(mktemp -d)"
trap 'kill "$SERVER" 2>/dev/null || true; rm -rf "$OUT"' EXIT
QT_QPA_PLATFORM=offscreen "$BIN" --serve 0 >"$OUT/server.log" 2>&1 &
SERVER=$!
for _ in $(seq 50); do
    PORT=$(sed -n 's/^quill-collab server on port \([0-9]*\)$/\1/p' "$OUT/server.log")
    [ -n "$PORT" ] && break
    sleep 0.1
done
[ -n "${PORT:-}" ] || { echo "server did not start:" >&2; cat "$OUT/server.log" >&2; exit 1; }

for i in 1 2 3; do
    node client.mjs --port "$PORT" --name "js$i" --edits 80 --seed "$i" >"$OUT/client$i.json" &
done
wait %2 %3 %4
node client.mjs --port "$PORT" --name observer --edits 0 >"$OUT/server.json"

status=0
for i in 1 2 3; do
    if ! cmp -s "$OUT/client$i.json" "$OUT/server.json"; then
        echo "client $i differs from the server:" >&2
        echo "  client: $(cat "$OUT/client$i.json")" >&2
        echo "  server: $(cat "$OUT/server.json")" >&2
        status=1
    fi
done
revisions=$(grep -c '^revision' "$OUT/server.log" || true)
[ "$status" = 0 ] && echo "3 quill-delta clients converged with the quill-collab server ($revisions updates): $(head -c 120 "$OUT/server.json")..."
exit $status
