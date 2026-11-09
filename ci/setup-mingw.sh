#!/usr/bin/env bash
# Sourced by ci/build.sh and ci/test.sh for the Mingw config: installs (once)
# llvm-mingw 20231128 (LLVM 17.0.6, what Qt 6.8.3's win64_llvm_mingw uses),
# Qt 6.8.3 for Windows and for the Linux host tools, and exports
#   MINGW_ROOT / QUILLQT_MINGW_ROOT  the toolchain
#   QT_WINDOWS                       Qt for Windows (CMAKE_PREFIX_PATH)
#   QT_HOST                          Linux Qt with moc / rcc (QT_HOST_PATH)
#   WINEPATH, WINEDEBUG              so Wine finds the Qt and C++ runtime DLLs
QUILLQT_ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
QUILLQT_MINGW_QT="${QUILLQT_MINGW_QT:-6.8.3}"
QUILLQT_MINGW_RELEASE="${QUILLQT_MINGW_RELEASE:-20231128}"
_cache="${QUILLQT_QT_ROOT:-$HOME/.cache/quillqt-qt}"
_name="llvm-mingw-${QUILLQT_MINGW_RELEASE}-ucrt-ubuntu-20.04-x86_64"
MINGW_ROOT="$_cache/toolchains/$_name"
if [ ! -x "$MINGW_ROOT/bin/x86_64-w64-mingw32-clang++" ]; then
    mkdir -p "$_cache/toolchains"
    curl -sSLf -o "$_cache/toolchains/llvm-mingw.tar.xz" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/${QUILLQT_MINGW_RELEASE}/${_name}.tar.xz"
    tar xf "$_cache/toolchains/llvm-mingw.tar.xz" -C "$_cache/toolchains"
    rm "$_cache/toolchains/llvm-mingw.tar.xz"
fi
QT_WINDOWS="$("$QUILLQT_ROOT_DIR/ci/install-qt.sh" "$QUILLQT_MINGW_QT" windows desktop win64_llvm_mingw)"
QT_HOST="$("$QUILLQT_ROOT_DIR/ci/install-qt.sh" "$QUILLQT_MINGW_QT")"
QUILLQT_MINGW_ROOT="$MINGW_ROOT"
WINEPATH="$(winepath -w "$QT_WINDOWS/bin" 2>/dev/null);$(winepath -w "$MINGW_ROOT/x86_64-w64-mingw32/bin" 2>/dev/null)"
WINEDEBUG="${WINEDEBUG:--all}"
export MINGW_ROOT QUILLQT_MINGW_ROOT QT_WINDOWS QT_HOST WINEPATH WINEDEBUG
