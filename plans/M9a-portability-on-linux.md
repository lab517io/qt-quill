# M9a — Portability groundwork on Linux

Estimate: 2–3 days. Depends on: M8 (CI scripts, oracle, flutter_quill check).
Comes before M9, which stays deferred until Windows and macOS machines exist.

## Goal

Do every part of M9 that does not need a Windows or macOS machine, so that
M9 itself shrinks to "build, run the suite, walk the manual checklist":

1. Separate **Qt-version** problems from **platform** problems: Windows and
   macOS CI will use Qt 6.8 LTS from the Qt installers, while everything so
   far ran on Ubuntu's Qt 6.4.2.
2. Catch **compiler** problems early with clang (Apple clang's front end)
   and a Windows cross-build (llvm-mingw, run under Wine).
3. Fix the **platform differences** from the M9 table that can be
   reproduced on Linux (fonts, file URLs, HiDPI, styles, clipboard HTML).
4. Write the **Windows and macOS CI jobs** and scripts, ready to run once a
   remote exists.

Nothing here changes the system: Qt versions and the cross toolchain are
installed in user directories with `aqtinstall` and release tarballs.

## Findings that shape the plan (checked 2026-11-09)

| Item | State |
|---|---|
| Qt via `aqtinstall` (pip, network OK) | 6.8.3 and 6.11.x for `linux_gcc_64`; 6.8.3 for Windows as `win64_llvm_mingw`, `win64_mingw`, `win64_msvc2022_64` |
| Compilers | GCC 13.2, clang 18.1.3 |
| Windows runtime | Wine with 64-bit support (no wine32; not needed) |
| llvm-mingw | release tarballs on GitHub; Qt 6.8's `win64_llvm_mingw` needs the matching LLVM 17 release |
| Fonts | fontconfig maps `monospace` / `serif` to DejaVu; Windows has no such families, so this hides a real difference |
| Disk | 63 GB free (one Qt version ≈ 1–1.5 GB) |

## Deliverables

```
ci/install-qt.sh                 aqtinstall wrapper: pinned versions into $QUILLQT_QT_ROOT (default ~/Qt)
ci/build.sh, ci/test.sh          new configs: Clang, Qt68, Qt611, Mingw (cross), HiDPI test mode
cmake/toolchains/llvm-mingw.cmake
ci/build.ps1, ci/test.ps1        Windows equivalents of build.sh / test.sh (unverified until M9)
.github/workflows/ci.yml         Linux matrix extended; build-windows / build-macos jobs (unverified)
tests/fixtures/paste/*.html      clipboard HTML from Windows (CF_HTML fragment), macOS Safari,
                                 Word, LibreOffice, Google Docs, Chrome
tests/widgets/tst_paste.cpp      paste fixtures → expected Deltas
plans/M9-checklist.md            the manual checklist M9 runs on each platform
README "Platform notes"          what differs, deployment commands (windeployqt / macdeployqt, unverified)
```

## Work items

### 1. Qt version matrix (6.4.2 / 6.8 LTS / 6.11)

- `ci/install-qt.sh 6.8.3 linux_gcc_64` (and `6.11.x`); `ci/build.sh Qt68`
  configures with `CMAKE_PREFIX_PATH` pointing at it.
- Run everything on each version: ctest, the long fuzz runs (3000 API steps,
  60 000 keys), `tst_flutter_parity`, screenshots (compare with 6.4.2 by
  eye).
- Expected trouble spots: `QTextEdit` list handling and markers, the
  `contentsChange` range quirk M5 worked around, undo/redo counts, HTML
  import for paste, `QTextDocumentLayout` object sizing, the benign
  `setPosition` warning in redo.
- Every behaviour difference gets a test that pins it, a fix in quill-qt (not
  a version `#if` unless unavoidable), and a row in an outcome table.
- Decide the supported floor: keep 6.4 (Ubuntu 24.04) if the fixes allow it.

### 2. Compilers

- `ci/build.sh Clang`: clang 18 with the same warning set; add an option
  `QUILLQT_WERROR` (on in CI) so new warnings fail the build.
- Review MSVC-isms the GCC/clang builds cannot see: `min`/`max` macros
  (`NOMINMAX` is set), `and`/`or` tokens, designated initialisers,
  `__attribute__`, implicit `const char*` → `QString` (already disabled),
  sizes as `int` vs `qsizetype` narrowing warnings MSVC emits (`/W4` C4267).

### 3. Windows cross-build under Wine (timeboxed: 1 day)

- llvm-mingw (LLVM 17, matching Qt 6.8.3 `win64_llvm_mingw`) and the Windows
  Qt from aqtinstall; host tools (moc, rcc) from Linux Qt 6.8.3 via
  `QT_HOST_PATH`; `cmake/toolchains/llvm-mingw.cmake`.
- Goal A (required): the library, demo and tests **compile and link** for
  Windows. That catches Windows headers, path and `wchar_t` issues, missing
  `<climits>` style includes, and export/static linking problems.
- Goal B (best effort): run `ctest` under `wine64` with
  `QT_QPA_PLATFORM=offscreen` (Windows Qt ships `qoffscreen.dll`). Failures
  are triaged: real Windows behaviour (fix), Wine limitation (document).
- If Goal B does not work within the timebox, keep compile-only in CI and
  say so in the outcome.

### 4. Platform differences reproducible on Linux

| Area | Work | Test |
|---|---|---|
| Generic font names | Quill/flutter_quill values `monospace`, `serif`, `sans-serif` map to `QFont::StyleHint` plus a family list (`QFontDatabase::systemFont(FixedFont)` for code), not a literal family that Windows lacks; export still writes the original value | a font-matching test that fails if the format relies on a family named "monospace"; run with a fontconfig config that hides DejaVu aliases |
| Checklist hit area | verify `blockWithMarkerAt` with other widget styles (Fusion, Windows style, both available on Linux) and fonts; size from the style if needed | `tst_editor_keys` checkbox click under `-style windows` and `-style fusion` |
| File URLs | audit every `QUrl` / path use in embeds, paste and drop; Windows paths (`file:///C:/...`, UNC `file://server/share/...`, backslashes in pasted text) round-trip unchanged and resolve with `QUrl::toLocalFile` | URL fixtures in `tst_embeds` / `tst_editor_changes` |
| HiDPI | run the widget suites with `QT_SCALE_FACTOR=2` (offscreen); icons, decorations, image embeds and hit-tests at device pixel ratio 2 | ctest variants `*_hidpi`; screenshots at 2× |
| Clipboard HTML | real clipboard captures as fixtures: Windows CF_HTML fragment (`<!--StartFragment-->`), macOS Safari (`<meta charset>`, Apple-converted spaces), Word (`mso-` styles), LibreOffice, Google Docs (`<b style="font-weight:normal" id="docs-internal-guid-...">`), Chrome | `tst_paste`: each fixture → expected Delta (formats kept, junk dropped, ignored attributes respected) |
| Line endings | CRLF / CR in plain-text paste and drops (already normalised in apply; pin it at the paste level) | `tst_paste` |
| Shortcuts | audit `Qt::ControlModifier` uses (Ctrl+K, Ctrl+Shift+X, Ctrl+click on links): on macOS Qt maps Control to Cmd, which is the intended key; document Cmd on macOS | review + README |
| TLS | `ImageLoader` when TLS is unavailable or a certificate is bad (schannel / SecureTransport differ): fails into the broken-image box, never blocks | `tst_embeds` with a stub that resets the connection / a bad `https` port |
| Dark mode | already palette-driven; add a test that every painted colour comes from the palette or `StyleOptions` under a dark palette | screenshots light/dark at 2× |

### 5. CI and scripts for M9

- `.github/workflows/ci.yml`: Linux matrix gains `Clang`, `Qt68`
  (aqtinstall-cached), `HiDPI`, `Mingw` (compile, plus Wine tests if goal B
  works). New jobs `build-windows` (MSVC 2022, Qt 6.8 via
  `jurplel/install-qt-action`) and `build-macos` (arm64, Qt 6.8), marked as
  not yet verified.
- `ci/build.ps1` / `ci/test.ps1` mirror the shell scripts.
- README "Platform notes": supported Qt range, fonts, shortcuts on macOS,
  deployment with `windeployqt` / `macdeployqt` (marked unverified).
- `plans/M9-checklist.md`: the manual session for each platform (lists,
  checklists, paste from browser and Word/Pages, image drag-drop, dark mode,
  HiDPI monitor, IME input such as Japanese and accents via dead keys).
- Update `plans/M9-windows-macos.md` with what M9a covered and what is left.

## Tasks

- [ ] `ci/install-qt.sh`; Qt 6.8.3 and 6.11.x installed; `Qt68` / `Qt611` configs.
- [ ] Full suite, fuzz, parity and screenshots on 6.8 and 6.11; fix and pin differences.
- [ ] `Clang` config, `QUILLQT_WERROR`; MSVC-ism review.
- [ ] llvm-mingw cross-build (compile); Wine test run (best effort, timeboxed).
- [ ] Generic font mapping via style hints; font test.
- [ ] Checklist hit-test under other styles.
- [ ] Windows file URL fixtures and audit.
- [ ] HiDPI test runs and fixes.
- [ ] Clipboard HTML fixtures and `tst_paste`; CRLF at paste level.
- [ ] TLS / connection failure tests for `ImageLoader`.
- [ ] CI matrix and Windows/macOS jobs; `build.ps1` / `test.ps1`.
- [ ] README platform notes; `M9-checklist.md`; M9 plan update.

## Exit criteria

- ctest green on GCC and clang with Qt 6.4.2, 6.8 and 6.11; no warnings
  with `QUILLQT_WERROR`.
- Same results at scale factor 2 and under the Fusion and Windows widget
  styles.
- The Windows cross-build compiles and links library, demo and tests; the
  Wine test run passes or its failures are explained.
- Paste fixtures from all listed sources produce the expected Deltas.
- M9 is reduced to running the prepared jobs and the manual checklist.

## Risks

- **Qt 6.11 is much newer than 6.4.** Behaviour changes in `QTextEdit` may
  take time; fix the ones that also hit 6.8 first, and treat 6.11-only ones
  as a forward-compatibility list if they get long.
- **Wine** may not run the Qt test binaries reliably (fonts, offscreen
  plugin); that part is timeboxed and optional.
- **llvm-mingw / Qt version mismatch** breaks linking; pin the LLVM release
  Qt 6.8.3 was built with.
- **Clipboard fixtures** need real captures; where we cannot capture from a
  platform here, use HTML published in Qt's and Quill's own clipboard tests
  and mark the provenance.
- Downloads (≈ 4 GB total) depend on the network being available.
