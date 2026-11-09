# M9 — Windows and macOS

**Deferred (2026-11-09):** no Windows or macOS machines yet. The parts that
can be done on Linux are in [M9a](M9a-portability-on-linux.md).

Estimate: 1 day per platform plus fixes. Depends on: M6 (demo to exercise),
M8 (CI to host the jobs).

## Goal

The same CMake tree builds and passes tests on Windows (MSVC 2022) and macOS
(Apple clang, Qt 6 universal or arm64), and the demo behaves the same as on
Linux. Nothing in M1–M8 is Linux-specific by design; this milestone verifies
that and fixes what differs.

## Deliverables

- CI jobs `build-windows` and `build-macos` (Qt via `jurplel/install-qt-action`,
  version 6.5 or newer LTS; apt 6.4.2 stays the Linux floor).
- `ci/build.ps1` or a cross-platform `ci/build.py` replacing `build.sh` if
  shell scripts become a burden; `ctest` with `QT_QPA_PLATFORM=offscreen`
  works on all three.
- Deployment recipes for the demo: `windeployqt`, `macdeployqt` (documented
  in README; not packaged installers).
- Platform notes section in README.

## Known platform differences to handle

| Area | Issue | Plan |
|---|---|---|
| Fonts | `monospace` and `serif` generic names resolve differently; Windows has no "monospace" family | use `QFontDatabase::systemFont(FixedFont)` for code and `QFont::StyleHint` for generic Quill names; export maps back to Quill names (M4 already does) |
| Font sizes | point vs pixel; macOS default 13pt, Windows 9pt | all size maths relative to `baseFont` (M3/M4), never absolute |
| Shortcuts | Ctrl vs Cmd | use `QKeySequence::StandardKey` (Bold, Italic, Underline exist) and `Qt::ControlModifier` (maps to Cmd on macOS) |
| Checklist marker rendering | drawn by `QTextDocumentLayout` using the style; size differs on Windows/Mac styles | hit-test area for checkbox toggle (M5) computed from `QStyle::pixelMetric(PM_IndicatorWidth)` not hard-coded |
| Clipboard HTML | Windows adds CF_HTML fragments, macOS Safari adds `<meta charset>`, `<b>` vs `font-weight` | M5 paste path normalises through `QTextDocument` import; add platform paste fixtures |
| Line endings | CRLF in pasted plain text | normalise `\r\n` → `\n` in `insertFromMimeData` |
| File URLs | `file:///C:/...` | use `QUrl::fromLocalFile`/`toLocalFile` everywhere embeds touch paths |
| HiDPI | image embeds and decorations at fractional scale | use `devicePixelRatioF()` for cached pixmaps; render tests at 1.0 and 2.0 |
| Networking | TLS backend on Windows (schannel) and macOS (SecureTransport) | `ImageLoader` must handle missing backend gracefully; CI test uses plain HTTP stub |
| Dark mode | macOS/Windows system dark palettes | decorations and icons from palette roles only (M6 rule) |
| MSVC | `/permissive-`, `NOMINMAX`, `std::min/max` macros, `and`/`or` not keywords | warnings module from M1 sets these; no `INT_MAX` without `<climits>` |
| Offscreen tests on macOS CI | occasional font fallback warnings | allow, do not assert on warnings |

## Tasks

- [ ] Add Windows and macOS jobs; fix compile errors (MSVC strictness).
- [ ] Run full test-suite offscreen on both; fix failures.
- [ ] Manual demo session on each: lists, checklists, paste from a browser
      and from Word/Pages, image drag-drop, dark mode.
- [ ] Platform paste fixtures and tests.
- [ ] README platform notes and deployment commands.

## Exit criteria

- CI matrix green on Linux, Windows, macOS.
- Demo manual checklist (kept in `plans/M9-checklist.md` once written)
  passes on each platform.

## Risks

- Qt 6.4 (apt) vs 6.5+ (CI installers) behavioural drift, especially in
  `QTextEdit` list handling; the Linux job should also build with the same
  `aqtinstall` Qt as the other platforms to separate Qt-version issues from
  platform issues.

## Notes from M8

- The CI workflow is GitHub Actions with Linux jobs only; add Windows and
  macOS build/test jobs calling the same `ci/*.sh` (or `.ps1` equivalents).
- `tests/lsan.supp` and the Asan config are Linux/GCC specific.
- `ci/check-flutter.sh` and `ci/check-oracle.sh` are platform independent and
  need to run on one platform only.
