# Milestone plans

Decisions fixed on 2026-11-08 (see `../PLAN.md`, section 8):

- Qt 6 only (6.4.2 from Ubuntu 24.04 is the floor; no Qt 5 ifdefs).
- flutter_quill compatibility first; Quill.js 2 specifics are accepted on input
  and preserved, but fixtures and toolbar defaults follow flutter_quill.
- Vendored sources: consumers `add_subdirectory()` this tree; no install or
  export rules are required, no shared library ABI promise.
- CI with a Node.js `quill-delta` oracle is acceptable.

| # | File | Scope | Depends on |
|---|---|---|---|
| 1 ✅ | [M1-environment-skeleton.md](M1-environment-skeleton.md) | Qt 6 install, CMake layout, offscreen test runner | — |
| 2 ✅ | [M2-delta-core.md](M2-delta-core.md) | `libquilldelta`: Delta/Op/AttributeMap, OT functions, JSON I/O, ported tests | 1 |
| 3 ✅ | [M3-delta-to-textdocument.md](M3-delta-to-textdocument.md) | Loading a Delta into `QTextDocument` | 2 |
| 4 ✅ | [M4-textdocument-to-delta.md](M4-textdocument-to-delta.md) | Exporting `QTextDocument` to Delta, normalisation, round-trip tests | 3 |
| 5 ✅ | [M5-editor-widget.md](M5-editor-widget.md) | `QuillEditor`: Quill-like API, change Deltas, keyboard behaviour, decorations | 4 |
| 6 ✅ | [M6-toolbar-demo.md](M6-toolbar-demo.md) | `QuillToolbar`, demo application, file I/O | 5 |
| 7 ✅ | [M7-embeds.md](M7-embeds.md) | Embed registry: image loading, video, formula, flutter_quill `custom` | 5 |
| 8 ✅ | [M8-fidelity-ci.md](M8-fidelity-ci.md) | quill-delta oracle, flutter_quill fixtures, CI pipeline | 2, 4 |
| 9a ✅ | [M9a-portability-on-linux.md](M9a-portability-on-linux.md) | Qt 6.8/6.12 and clang on Linux, Windows cross-build under Wine, platform differences reproducible on Linux, CI jobs for M9 | 8 |
| 9 ⏸ | [M9-cross-platform.md](M9-cross-platform.md) | Windows and macOS builds, platform differences (deferred: no Windows/macOS machines yet) | 6, 9a |
| 10 ✅ | [M10-rendering-fidelity.md](M10-rendering-fidelity.md) | Spacing measured in flutter_quill: block embeds, group spacing by attributes, code panel padding, formula `script` | 7, 8 |
| 11 | [M11-large-documents.md](M11-large-documents.md) | Benchmarks and budgets for big documents, fixes for the hot paths, scaling check in ctest | 5, 8 |
| 12 | [M12-collaboration.md](M12-collaboration.md) | Collaboration example: client–server OT, Delta undo history, remote cursors, `quill-collab` app | 2, 5, 6, 11 |
| 13 | [M13-image-resize.md](M13-image-resize.md) | Image resize handles, size written the flutter_quill way | 7, 10 |

Each plan has the same sections: Goal, Deliverables, Design, Tasks, Tests,
Exit criteria, Risks. Tasks are checkboxes so progress can be tracked by
editing the file.

Naming used throughout: namespace `quill`, CMake targets `QuillQt::Delta`
and `QuillQt::Widgets`, source roots `src/delta/` and `src/widgets/`.
