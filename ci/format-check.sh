#!/usr/bin/env bash
# Fails when C++ sources are not formatted with .clang-format.
# Uses clang-format-18 (ubuntu-24.04) or $CLANG_FORMAT; with --fix, formats.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CF="${CLANG_FORMAT:-$(command -v clang-format-18 || command -v clang-format || true)}"
[ -n "$CF" ] || { echo "clang-format not found (set CLANG_FORMAT)" >&2; exit 1; }
"$CF" --version
cd "$ROOT"
mapfile -t files < <(git ls-files --cached --others --exclude-standard -- \
    'src/*.cpp' 'src/*.h' 'apps/*.cpp' 'apps/*.h' 'tests/*.cpp' 'tests/*.h')
if [ "${1:-}" = "--fix" ]; then
    "$CF" -i --style=file "${files[@]}"
else
    "$CF" --dry-run -Werror --style=file "${files[@]}"
    echo "${#files[@]} files formatted correctly"
fi
