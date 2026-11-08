# Native Qt Widgets Quill editor — feasibility investigation and plan

Date: 2026-11-08. Status: investigation only, no code written yet.

## 1. Verdict

A native QtWidgets editor that reads and writes the Quill Delta format and
behaves like Quill / flutter_quill is feasible, with moderate effort, if the
editor is built on `QTextDocument` + `QTextEdit` and Delta is treated as the
canonical interchange and change format. Everything Quill's core format set
needs (inline styles, links, headers, lists incl. checklists, block quotes,
code blocks, alignment, indent, RTL direction, inline images) has a direct or
near-direct counterpart in Qt's rich text framework. The two places that need
custom work are embeds other than images (video, formula, custom) and
flutter_quill extras (line-height, tables from the extensions package).

Writing a layout engine from scratch (a C++ Parchment) is also possible but
not recommended as a first step: it would re-implement text layout, bidi,
IME, selection, clipboard and accessibility that `QTextDocument` already
gives for free.

## 2. What Quill and flutter_quill actually are

Both share one data model, the **Delta** (`slab/delta`, BSD-3). A Delta is a
JSON list of operations:

```json
{"ops":[
  {"insert":"Hello ","attributes":{"bold":true}},
  {"insert":"world"},
  {"insert":"\n","attributes":{"header":1}},
  {"insert":{"image":"https://x/y.png"}},
  {"insert":"\n"}
]}
```

Facts that drive the design:

- A **document** Delta contains only `insert` ops and always ends with `\n`.
- A **change** Delta uses `insert`, `retain(n, attrs?)`, `delete(n)`. Ops have no
  index; each applies at the running position. `null` attribute value means
  "remove this attribute".
- **Block (line) attributes live on the `\n` character** of the line:
  `header`, `list`, `blockquote`, `code-block`, `align`, `direction`, `indent`
  (and `line-height` in flutter_quill). Inline attributes live on text runs.
- **Embeds** are `insert` ops whose value is an object with exactly one key
  (`image`, `video`, `formula`, `custom`, or any app-defined key). An embed
  has length 1.
- The library also defines `compose`, `transform`, `transformPosition`,
  `diff`, `invert`, `slice`, `concat`, `eachLine`. These are what make
  collaboration (OT) and undo work. The attribute-map rules (how `null` is
  handled in compose/transform/invert/diff) are small and precisely
  specified in `AttributeMap.ts`; porting them is mechanical.
- Quill's architecture is Delta (model) ↔ Parchment blot tree (view model)
  ↔ DOM. flutter_quill mirrors this with Delta ↔ `Document`/`Node` tree ↔
  Flutter render objects. Neither layer above Delta needs to be ported
  literally; only the Delta semantics and the attribute set must match.

### Attribute inventory to support

| Quill attribute | Values | Scope | Qt counterpart |
|---|---|---|---|
| bold, italic, underline, strike | true | inline | `QTextCharFormat` weight / italic / underline / strikeOut |
| code | true | inline | `QTextCharFormat` fixed-pitch font + background, user property |
| script | sub, super | inline | `QTextCharFormat::setVerticalAlignment` |
| link | URL | inline | `QTextCharFormat::setAnchor/setAnchorHref` |
| color, background | CSS color | inline | foreground / background brush |
| font | family name (Quill: serif, monospace; fq: free) | inline | `setFontFamilies` |
| size | small, large, huge (Quill) or free string (fq) | inline | point size via a size map + user property keeping the original string |
| small (fq only) | true | inline | smaller point size, user property |
| header | 1–6 | block | `QTextBlockFormat::setHeadingLevel` + font scaling char format |
| list | bullet, ordered, checked, unchecked | block | `QTextList` with `QTextListFormat` style; checked/unchecked via `QTextBlockFormat::setMarker` (Qt ≥ 5.14) |
| indent | int | block | `QTextListFormat::setIndent` for list items, `QTextBlockFormat::setIndent` otherwise |
| align | left, center, right, justify | block | `QTextBlockFormat::setAlignment` |
| direction | rtl | block | `QTextBlockFormat::setLayoutDirection` |
| blockquote | true | block | left margin + border drawn by subclass; user property |
| code-block | true (fq) or language string (Quill 2) | block | monospace char format, background, user property holding the language |
| line-height (fq only) | string | block | `QTextBlockFormat::setLineHeight` |
| image | URL | embed | `QTextImageFormat` (inline, length 1 like Quill) |
| video | URL | embed | custom `QTextObjectInterface` placeholder with poster/URL |
| formula | TeX string | embed | custom text object; render source in monospace at first, optional later renderer |
| custom (fq) | JSON string | embed | custom text object; raw payload preserved for round-trip |
| width, height, style, token, placeholder (fq) | misc | ignore | preserved as user properties so they round-trip |

Everything in the table is representable. Nothing forces data loss on a
Delta → Qt → Delta round-trip as long as unknown attributes are kept as
opaque user properties on the format objects.

## 3. Architecture options considered

### A. `QTextDocument` as the live model, Delta as the canonical format (recommended)

- Editor = `QTextEdit` subclass. Qt supplies layout, cursor, selection,
  bidi, IME, clipboard, drag and drop, undo/redo, printing, accessibility.
- Two converters: `Delta → QTextDocument` (load) and `QTextDocument → Delta`
  (save). Both are straightforward walks (block iterator + fragment
  iterator on one side, `eachLine` on the other).
- Change Deltas (Quill's `text-change` event) come from
  `QTextDocument::contentsChange(position, removed, added)`: emit
  `retain(position) · delete(removed) · insert(<formatted text of the added
  range>)`. Formatting-only changes report `removed == added`, which maps to
  `retain(n, attrs)` after diffing the formats of the range. The full
  `diff` from the ported Delta library serves as a fallback/validation.
- Risk: `QTextEdit` produces structures Delta cannot express (soft line
  breaks `U+2028`, frames, tables, nested lists via `QTextList` objects
  rather than per-line indent). Mitigation: a normalisation pass on export
  and an input filter that keeps the document in "Quill-shaped" form
  (one block per line, lists flattened to `list` + `indent`).

### B. Embed the real Quill.js in `QWebEngineView` with a `QWebChannel` bridge

- Fastest route to 100 % format fidelity and feature parity.
- Not native widgets: pulls in Chromium (~150 MB), slower start-up, theming
  and focus handling differ from the rest of a Qt app. Rejected for the
  stated goal, but worth keeping as a reference oracle in tests (compare
  rendering / Delta output of the native editor against Quill.js in CI).

### C. Custom widget with own layout engine (port of Parchment / flutter_quill rendering)

- Maximal control, exact Quill behaviour for cursor and block semantics.
- Months of work before it reaches what `QTextDocument` provides on day one.
  Keep as a long-term option if option A hits fidelity walls.

Decision: option A. Design the public API so the backing document could be
replaced later (editor exposes Deltas, not `QTextDocument`).

## 4. Component plan

```
quill-qt/
  libquilldelta/      pure C++/Qt-Core: Delta, Op, AttributeMap, JSON I/O,
                      compose/transform/transformPosition/diff/invert/slice/
                      concat/eachLine. No GUI dependency. Unit-tested against
                      the slab/delta test-suite (fixtures are BSD-3; port the
                      cases, not the code).
  libquillwidgets/    Qt Widgets:
                      DeltaDocument      Delta <-> QTextDocument converters,
                                         normalisation, format registry
                      QuillEditor        QTextEdit subclass: setDelta(),
                                         delta(), textChanged(delta, oldDelta,
                                         source), selectionChanged(range),
                                         format(name, value), getFormat()
                      QuillToolbar       actions bound to the editor
                      Embeds             image, video placeholder, formula
                                         placeholder, custom; registry so
                                         apps add their own
  apps/quill-demo/    editor + toolbar + open/save .json Delta, HTML export
  tests/              QtTest: delta unit tests, round-trip fixtures,
                      change-delta tests driven through QTextCursor
```

Language and build: C++17, CMake, Qt 6 (6.4 is in the Ubuntu 24.04 repos;
6.5+ LTS preferred when available). The `QTextBlockFormat::MarkerType`
checklist API requires Qt ≥ 5.14, so Qt 5.15 LTS can also be supported with
small ifdefs if needed.

## 5. Milestones

1. [plan](plans/M1-environment-skeleton.md) **Environment and skeleton (½ day).** Install `qt6-base-dev`,
   `qt6-base-dev-tools`, `libgl1-mesa-dev` (needed by Qt GUI on headless
   builds); CMake project with the three targets; CI-style script using
   `xvfb-run` for GUI tests since this machine has no display.
2. [plan](plans/M2-delta-core.md) **libquilldelta (2–3 days).** `Op`, `AttributeMap`, `Delta` with JSON
   (`QJsonDocument`) in/out, `length`, `slice`, `concat`, `eachLine`,
   `compose`, `transform`, `transformPosition`, `invert`, `diff`
   (`diff` needs a string diff; use a small Myers/diff-match-patch style
   implementation on the embed-substituted text as slab/delta does).
   Port the slab/delta test cases. Exit criterion: all ported cases pass.
3. [plan](plans/M3-delta-to-textdocument.md) **Delta → QTextDocument (2 days).** Inline formats, headers, lists incl.
   checklists and `indent`, blockquote, code-block, align, direction,
   images. Unknown attributes preserved as user properties. Fixture Deltas
   from Quill docs and flutter_quill samples.
4. [plan](plans/M4-textdocument-to-delta.md) **QTextDocument → Delta and round-trip (2 days).** Normalisation rules;
   round-trip tests `delta == toDelta(fromDelta(delta))` over the fixtures.
   Export the document Quill would produce for a document typed in the
   editor (trailing `\n`, block attributes on newline only).
5. [plan](plans/M5-editor-widget.md) **QuillEditor widget (3 days).** `QTextEdit` subclass; `setDelta/delta`;
   change-delta emission from `contentsChange`; `format()` API mirroring
   Quill's `format/formatLine/formatText`; keyboard behaviour that Quill
   users expect (Enter in a list continues the list, Enter on an empty list
   item exits, Backspace at list start removes the list format, Tab/Shift+Tab
   indent, Ctrl+B/I/U, Enter in code-block stays in code-block, checkbox
   toggles on click). Blockquote and code-block decorations painted in
   `paintEvent`.
6. [plan](plans/M6-toolbar-demo.md) **Toolbar and demo app (2 days).** Quill's default toolbar set; open and
   save Delta JSON; show the live Delta in a side pane for debugging; HTML
   export via `QTextDocument::toHtml` for convenience.
7. [plan](plans/M7-embeds.md) **Embeds beyond images (2–3 days).** `QTextObjectInterface` text objects
   for video, formula and custom; registry API so an application can plug in
   its own renderer per embed key; payload round-trips untouched.
8. [plan](plans/M8-fidelity-ci.md) **Fidelity pass against Quill.js (ongoing).** Optional: a Node script
   that loads each fixture into quill-delta and compares `compose/transform`
   results and `getContents()` with the C++ output. Also verify
   flutter_quill produced Deltas (e.g. `size` numeric strings, `line-height`,
   `custom` embeds) load and save unchanged.
9. [plan](plans/M9-cross-platform.md) **Cross-platform (1 day each, later).** Windows and macOS builds of the
   same CMake project; nothing in the plan is Linux-specific. Clipboard
   fidelity (paste from browsers) and font fallbacks are the expected
   platform differences.

Estimated total for a usable Linux editor through milestone 6: roughly two
to three weeks of focused work. Embeds and fidelity tooling add one more.

## 6. Known gaps and how they are handled

- **Nested lists.** Quill models nesting as flat lines with `indent`.
  Qt's `QTextList` groups blocks; different indent levels need separate
  `QTextList` objects per (type, indent) run. Numbering restarts are decided
  by the same adjacency rule Quill uses. Handled in the converters.
- **Soft line breaks (Shift+Enter).** Not representable in Delta. The editor
  disables them or converts to a hard block on export.
- **Tables.** Core Quill has none (the table module is experimental);
  flutter_quill tables live in an extensions package. Out of scope for the
  first release; `QTextTable` exists if needed later.
- **Formula rendering.** Quill uses KaTeX. Phase 1 shows the TeX source in a
  styled inline object; a later phase can render via a lightweight TeX to
  SVG library or a web view only for that object.
- **Size and font semantics.** Quill uses named sizes and a whitelist of
  fonts; flutter_quill accepts arbitrary values. The converter keeps the
  original string and applies a mapping table for display, so round-trip
  stays exact.
- **Colour strings.** Quill writes CSS colours (`#rrggbb`, `rgb(...)`,
  names). `QColor::fromString` handles hex and names; add a small parser
  for `rgb()/rgba()` and write back the original string when unchanged.
- **Code-block value.** Quill 2 stores the language as the value;
  flutter_quill stores `true`. Accept both, preserve whichever was read.
- **Undo/redo and OT.** `QTextDocument` has its own undo stack; the Delta
  `invert` port provides Quill-style history if collaboration is added
  later. The two must not be mixed; pick Qt's stack for the single-user
  editor and expose change Deltas for external sync.

## 7. Licensing

Quill and slab/delta are BSD-3-Clause, flutter_quill is MIT, Qt is
LGPLv3/commercial. A clean-room C++ reimplementation of the Delta algorithms
plus ported test fixtures (with the BSD notice) is compatible with any
licence chosen for this project.

## 8. Decisions (fixed 2026-11-08)

1. **Qt 6 only.** Qt 6.4.2 from the Ubuntu 24.04 repos is the minimum; no
   Qt 5 support.
2. **flutter_quill compatibility first.** Fixtures, JSON shape (bare array
   of ops), size/colour value forms and toolbar defaults follow
   flutter_quill; Quill.js 2 forms are accepted on input and preserved.
3. **Vendored sources.** Consumers `add_subdirectory()` the tree and link
   static targets `QuillQt::Delta` / `QuillQt::Widgets`; no install rules,
   no ABI promise.
4. **CI with a Node.js `quill-delta` oracle is acceptable.**

Detailed per-milestone plans live in [`plans/`](plans/README.md).

## 9. Immediate next steps

1. Execute [M1](plans/M1-environment-skeleton.md): install Qt 6, create the
   CMake skeleton, `git init`, run the offscreen smoke test.
2. Execute [M2](plans/M2-delta-core.md): the Delta core has no GUI
   dependency and a well-defined ported test-suite.
3. [M8](plans/M8-fidelity-ci.md)'s oracle can start right after M2 and run in
   parallel with M3/M4.

## Sources

- Quill Delta docs: https://quilljs.com/docs/delta and
  https://v2.quilljs.com/guides/designing-the-delta-format
- Quill formats list: https://quilljs.com/docs/formats
- slab/delta library (API, AttributeMap semantics, BSD-3): https://github.com/slab/delta
- flutter_quill attributes: https://github.com/singerdmx/flutter-quill/blob/master/lib/src/document/attribute.dart
- flutter_quill embeds: https://github.com/singerdmx/flutter-quill/blob/master/lib/src/document/nodes/embeddable.dart
- Qt `QTextBlockFormat::MarkerType` (checklists, Qt ≥ 5.14): https://doc.qt.io/qt-6/qtextblockformat.html
- KDE kpimtextedit checklist toggle reference: https://invent.kde.org/pim/kpimtextedit/-/merge_requests/2
