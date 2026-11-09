# Cross-compiling for Windows x86_64 with llvm-mingw (https://github.com/mstorsjo/llvm-mingw),
# matching Qt's win64_llvm_mingw packages. Set up by ci/setup-mingw.sh, which
# exports QUILLQT_MINGW_ROOT (environment, so try_compile sees it too).
# Tests run under Wine through CMAKE_CROSSCOMPILING_EMULATOR.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_root "$ENV{QUILLQT_MINGW_ROOT}")
if(NOT _root)
    message(FATAL_ERROR "QUILLQT_MINGW_ROOT is not set (source ci/setup-mingw.sh)")
endif()
set(_triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER "${_root}/bin/${_triple}-clang")
set(CMAKE_CXX_COMPILER "${_root}/bin/${_triple}-clang++")
set(CMAKE_RC_COMPILER "${_root}/bin/${_triple}-windres")
set(CMAKE_AR "${_root}/bin/llvm-ar")
set(CMAKE_RANLIB "${_root}/bin/llvm-ranlib")

set(CMAKE_FIND_ROOT_PATH "${_root}/${_triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

find_program(_wine NAMES wine64 wine)
if(_wine)
    set(CMAKE_CROSSCOMPILING_EMULATOR "${_wine}")
endif()
