#!/usr/bin/env bash
# Installs a Qt version with aqtinstall into $QUILLQT_QT_ROOT (default
# ~/.cache/quillqt-qt), without touching the system. Only qtbase is needed
# (Core, Gui, Widgets, Network, Test).
# Usage: ci/install-qt.sh <version> [host] [target] [arch]
#   ci/install-qt.sh 6.8.3                              -> linux desktop linux_gcc_64
#   ci/install-qt.sh 6.8.3 windows desktop win64_llvm_mingw
# Prints the Qt prefix (the directory to pass as CMAKE_PREFIX_PATH).
set -euo pipefail
VERSION="${1:?usage: install-qt.sh <version> [host] [target] [arch]}"
HOST="${2:-linux}"
TARGET="${3:-desktop}"
ARCH="${4:-linux_gcc_64}"
ROOT="${QUILLQT_QT_ROOT:-$HOME/.cache/quillqt-qt}"
AQT_VERSION=3.3.0
VENV="$ROOT/.aqt-venv"
if [ ! -x "$VENV/bin/aqt" ]; then
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q "aqtinstall==$AQT_VERSION"
fi
# aqt names the install directory after the arch, mostly without the host prefix.
case "$ARCH" in
    linux_gcc_64) dir=gcc_64 ;;
    win64_llvm_mingw) dir=llvm-mingw_64 ;;
    win64_mingw) dir=mingw_64 ;;
    win64_msvc2022_64) dir=msvc2022_64 ;;
    *) dir="$ARCH" ;;
esac
PREFIX="$ROOT/$VERSION/$dir"
if [ ! -f "$PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    "$VENV/bin/aqt" install-qt "$HOST" "$TARGET" "$VERSION" "$ARCH" --outputdir "$ROOT" >&2
fi
[ -f "$PREFIX/lib/cmake/Qt6/Qt6Config.cmake" ] || { echo "Qt not found in $PREFIX" >&2; exit 1; }
echo "$PREFIX"
