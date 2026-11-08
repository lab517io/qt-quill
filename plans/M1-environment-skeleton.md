# M1 — Environment and project skeleton

Estimate: ½ day. Depends on: nothing.

## Goal

A building, testable, empty project: Qt 6 installed on this Linux host, a
CMake tree with the final target layout, a headless test runner, and a smoke
test that proves `QTextEdit` works without a display and that Qt document
positions match Delta positions (needed by M5).

## Deliverables

```
quill/
  CMakeLists.txt                 top level; options; add_subdirectory()
  cmake/
    QuillQtWarnings.cmake        -Wall -Wextra -Wpedantic, /W4 on MSVC
  src/
    delta/CMakeLists.txt         static lib quillqt_delta  -> QuillQt::Delta  (Qt6::Core only)
    widgets/CMakeLists.txt       static lib quillqt_widgets -> QuillQt::Widgets (Qt6::Widgets, Qt6::Network)
  apps/demo/CMakeLists.txt       quill-demo (built when QUILLQT_BUILD_DEMO)
  tests/
    CMakeLists.txt               QtTest, add_test() for each tst_*
    fixtures/                    JSON Deltas (filled by M2–M4, M8)
    smoke/tst_smoke.cpp          see "Smoke test"
  ci/
    build.sh                     configure + build (Ninja, Release and Debug)
    test.sh                      QT_QPA_PLATFORM=offscreen ctest --output-on-failure
    install-deps-ubuntu.sh       apt packages below
  .clang-format                  Qt-ish style (4 spaces, 100 cols, Attach braces)
  .gitignore                     build/, *.user
  README.md                      replaces README.txt; vendoring instructions
```

## Design

### Packages (Ubuntu 24.04)

```
sudo apt-get install -y qt6-base-dev qt6-base-dev-tools libgl1-mesa-dev \
    libxkbcommon-dev ninja-build
```

`libgl1-mesa-dev` is required for `find_package(Qt6 Widgets)` to configure on a
headless box. Qt 6.4.2 is what apt provides and is the minimum version the
project declares (`find_package(Qt6 6.4 ...)`). Node.js is installed in M8,
not here.

### Vendoring contract

A consumer does:

```cmake
add_subdirectory(third_party/quill-qt)
target_link_libraries(myapp PRIVATE QuillQt::Widgets)
```

Rules that follow from "vendored sources":

- Libraries are `STATIC`. No `install()`, no export sets, no version script.
- `QUILLQT_BUILD_DEMO` and `QUILLQT_BUILD_TESTS` default to `ON` only when
  `PROJECT_IS_TOP_LEVEL`, else `OFF`, so a consumer's build is not polluted.
- No `CMAKE_CXX_STANDARD` set globally; set `cxx_std_17` as a
  `target_compile_features` on each target.
- `CMAKE_AUTOMOC ON` set per directory, not globally.
- Public headers under `src/<lib>/include/quill/...`; included as
  `<quill/Delta.h>`. Private headers next to sources.
- No `qt_standard_project_setup()` (it mutates the consumer's settings); use
  `qt_add_library`/`qt_add_executable` only where harmless, otherwise plain
  `add_library`.

### Headless testing

Use the `offscreen` QPA platform for unit tests (`QT_QPA_PLATFORM=offscreen`
exported in `ci/test.sh`); it needs no X server. Keep `xvfb-run` for the
demo screenshot job in M6/M8 only, because `offscreen` cannot show real
windows but can render `QTextDocument` to `QImage`, which is all tests need.

### Smoke test (tests/smoke/tst_smoke.cpp)

1. Create a `QTextDocument`, insert "ab", `insertBlock()`, insert "c", then
   an image via `QTextImageFormat`. Assert `characterCount() == 6`
   (a, b, separator, c, image, final terminator). This confirms Qt indexing
   equals Delta indexing including the trailing `\n` of a Delta document.
   Record the result in the file as a comment; M5 relies on it.
2. Create a `QTextEdit` in offscreen mode, `setPlainText`, `QTest::keyClicks`,
   read back. Confirms widgets + QtTest work headless.
3. `QTextBlockFormat::setMarker(Checked)` on a list item and render the
   document to a `QImage`; assert the image is not blank. Confirms the
   checklist API is present in the installed Qt.

## Tasks

- [x] Run `ci/install-deps-ubuntu.sh`; record `qmake6 -query QT_VERSION`.
- [x] Write top-level `CMakeLists.txt` with options and warnings module.
- [x] Create `src/delta` and `src/widgets` with one placeholder source each
      so the targets link (`quill::version()` function).
- [x] Create `tests/` with QtTest discovery and the smoke test.
- [x] `ci/build.sh` builds Debug and Release into `build/debug`, `build/release`.
- [x] `ci/test.sh` runs ctest offscreen; exit code propagates.
- [x] `git init`, first commit (the directory is not a repository yet).
- [x] Replace `README.txt` with `README.md` (goal, status, vendoring snippet,
      how to build and test). Keep the original wording in a "Motivation" section.

## Tests

- `tst_smoke` passes under `QT_QPA_PLATFORM=offscreen`.
- A scratch consumer project (in the scratchpad, not committed) that
  `add_subdirectory()`s this tree builds without enabling demo or tests.

## Exit criteria

- `ci/build.sh && ci/test.sh` succeeds on this host from a clean checkout.
- `characterCount()` finding documented in the smoke test.
- Checklist marker rendering confirmed on Qt 6.4.2.

## Risks

- apt Qt 6.4.2 lacks a fix that later milestones need (for example checklist
  mouse toggling in `QTextEdit`). Mitigation: M5 has a custom-handling
  fallback; if more is missing, switch to `aqtinstall` for Qt 6.5+ in CI and
  document the floor.
- Headless OpenGL warnings from `offscreen`. Harmless; set
  `QT_LOGGING_RULES=qt.qpa.*=false` in `ci/test.sh` if noisy.

## Outcome (2026-11-08)

- Qt 6.4.2 installed from apt; `ci/build.sh && ci/test.sh` green in Debug
  and Release with zero compiler warnings.
- Position fact confirmed: `characterCount()` of the test document is 6,
  equal to the Delta length. Block separators and the final terminator are
  `U+2029`; images are `U+FFFC`.
- Checklist markers render and differ between none, unchecked and checked.
- Qt 6.4.2 `QTextEdit` toggles checklist markers on click by itself; noted
  in M5.
- A scratch consumer using `add_subdirectory()` builds and links
  `QuillQt::Widgets` without building the demo or tests.
- Deviation from the plan: no `qt_add_library`; plain `add_library` with a
  `quillqt_target_defaults()` helper keeps consumer settings untouched.
  `QT_NO_CAST_FROM_ASCII` and related strictness defines are on for all
  quill-qt targets.
