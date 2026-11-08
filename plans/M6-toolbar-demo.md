# M6 — Toolbar and demo application

Estimate: 2 days. Depends on: M5.

## Goal

A reusable `QuillToolbar` bound to a `QuillEditor`, covering the
flutter_quill default toolbar, and a demo application that loads and saves
flutter_quill JSON, shows the live Delta and change stream, and serves as the
manual test bed and screenshot source for CI.

## Deliverables

```
src/widgets/include/quill/QuillToolbar.h
src/widgets/QuillToolbar.cpp LinkDialog.cpp ColorButton.cpp
src/widgets/icons/*.svg + quillqt_icons.qrc     (own simple icons or Breeze-style, licence noted)
apps/demo/main.cpp MainWindow.cpp DeltaInspector.cpp
apps/demo/samples.qrc                           fixtures embedded for "Load sample"
tests/widgets/tst_toolbar.cpp
ci/screenshot.sh                                xvfb-run demo --screenshot out.png per sample
```

## Design

### QuillToolbar

`QToolBar` subclass; `setEditor(QuillEditor*)`. Buttons call
`editor->format(name, value)`; state is refreshed from
`editor->getFormat()` on `editorChanged()`. Groups, following
flutter_quill's `QuillSimpleToolbar` defaults:

| Group | Controls | Attribute / action |
|---|---|---|
| History | undo, redo | `QTextEdit::undo/redo` |
| Font | font family combo (flutter_quill default list + "Clear"), size combo (`small`, `large`, `huge`, 10, 12, 14, 16, 18, 20, 24, 32, 48, "Clear") | `font`, `size` |
| Inline | bold, italic, underline, strike, inline code, subscript, superscript, small | `bold`, `italic`, `underline`, `strike`, `code`, `script`, `small` |
| Colour | text colour, background colour (popup palette + "More..." `QColorDialog`, "Clear") | `color`, `background` as `#rrggbb` (or `#aarrggbb` when the document uses ARGB) |
| Clear | clear formatting | `removeFormat` |
| Header | combo Normal, H1–H6 | `header` |
| Lists | bullet, numbered, checklist, indent −, indent + | `list`, `indent` |
| Align | left, center, right, justify; RTL toggle | `align`, `direction` |
| Blocks | blockquote, code block | `blockquote`, `code-block` |
| Insert | link (dialog with text + URL, edit existing), image (file dialog → `file://` URL, or URL prompt), video (URL prompt), formula (text prompt) | `link`, `insertEmbed` |
| Search | find bar toggle (flutter_quill has search) | `QTextDocument::find` |

Every control has a `QAction` exposed via `action(const QString& name)` so a
consumer can place them in menus or hide groups
(`setGroupVisible("colour", false)`). Icons are embedded SVGs; the toolbar
honours the application palette for dark themes.

### Demo application (`quill-demo`)

- `QMainWindow`: `QuillToolbar` on top, `QuillEditor` central, right dock
  `DeltaInspector` with two tabs: "Document" (pretty-printed Delta JSON,
  updates on change, debounced 100 ms) and "Changes" (list of `textChanged`
  change Deltas with source; click an entry to show its `oldContents`).
- File menu: New, Open (`.json`, accepts both JSON shapes), Save, Save As
  (flutter_quill bare-array shape by default, `{"ops"}` via a checkbox in
  the dialog), Export HTML (`QTextDocument::toHtml`, convenience only),
  Export plain text, Load sample → one entry per fixture.
- View menu: toggle inspector, read-only mode, dark palette (Fusion with a
  dark `QPalette`) to check decorations and icons.
- Command line: `quill-demo [file.json] [--screenshot out.png]`; the
  screenshot flag renders the window and exits (used by `ci/screenshot.sh`
  under `xvfb-run`, which needs a real window for `grab()`).
- Status bar: cursor index, selection length, current line attributes.

### Dialogs and widgets

- `LinkDialog`: text and URL fields; when the selection is inside an anchor
  pre-fill and offer Remove.
- `ColorButton`: tool button with colour swatch and popup of 20 colours
  (same palette flutter_quill shows) plus "More…" and "Clear".

## Tasks

- [x] `QuillToolbar` groups, actions, state sync, visibility API.
- [x] Icons (`.svg`) and resource file; dark-palette check.
- [x] `LinkDialog`, `ColorButton`.
- [x] Demo main window, inspector, file I/O, samples, screenshot flag.
- [x] `ci/screenshot.sh` producing one PNG per fixture into
      `build/*/screenshots/`.
- [x] `tst_toolbar`: toggling each action produces the expected change
      Delta; selecting formatted text updates button checked states; header
      combo reflects the current line.
- [x] README section "Demo" with a screenshot.

## Tests

- `tst_toolbar` offscreen.
- `ci/screenshot.sh` runs without errors under `xvfb-run -a`.

## Exit criteria

- Every flutter_quill default toolbar feature has a working control.
- Opening each flutter_quill sample, editing, saving, and re-opening in the
  flutter_quill example app (manual, on a machine with Flutter) shows no
  loss. Until that machine exists, the M8 oracle covers the Quill.js side.

## Risks

- Icon licensing: draw simple own icons or use a permissively licensed set
  (e.g. Lucide, ISC); record in `THIRD_PARTY_NOTICES.md`.
- `QColorDialog` is modal and heavy in tests; test via the palette popup
  path only.

## Notes from M5

- Use `format(name, value, Source::User)`; with a collapsed selection it sets
  the typing format and emits no textChange, so refresh button states on
  `QTextEdit::currentCharFormatChanged` as well as `editorChange`.
- `getFormat()` returns arrays for mixed values (Quill's combineFormats);
  show such buttons as unchecked / indeterminate.
- `linkRequested()` (Ctrl+K) is emitted for the toolbar's link dialog.
- `quill-demo [file.json] [--log]` already exists as a minimal stand-in.
- Toolbar configuration (the plan's `setReadOnlyFormats`) was left to M6.

## Outcome (2026-11-08)

Done. Debug, Release and ASan/UBSan builds clean, no leaks from quill-qt or
demo code; 15 ctest entries green (`tst_toolbar` and a demo smoke test
added).

- `QuillToolbar` with the groups and action names of the plan
  (`action(name)`, `groupActions(group)`, `setGroupVisible`), combo choices
  configurable (`setFontFamilies`, `setFontSizes`, `setPaletteColors`), state
  synced on `editorChange`, cursor moves and `currentCharFormatChanged`,
  everything disabled for a read-only editor. Dialogs are virtual
  (`promptLink`, `promptImage`, `promptVideo`, `promptFormula`,
  `promptColor`), which is also how `tst_toolbar` scripts them.
- `QuillFindBar` (public): highlights all matches as extra selections (the
  document is not changed), Enter / Shift+Enter / Escape, match case, count,
  follows edits; the toolbar's search action toggles it.
- Demo: toolbar, editor, find bar, "Delta" dock (document, one op per line in
  Delta key order; change stream with source, details with old contents),
  File (New, Open, Load sample, Save, Save As, Quill.js shape toggle, Export
  HTML / plain text), Edit, View (inspector, read-only, dark palette),
  status bar (index, selection, length, line attributes), command line
  `[file] --sample --dark --log --screenshot`. 10 embedded samples (all
  fixtures plus `showcase.json`).
- `ci/screenshot.sh`: light and dark PNG per sample into
  `build/<cfg>/screenshots/`, xvfb-run when present, offscreen otherwise.

`tst_toolbar` (15 cases): action and group listing, every inline toggle,
typing format on a collapsed selection, exclusive scripts, lists and
indent, alignment and direction, exclusive blocks, header / font / size
combos (including an unlisted size such as `"15.0"`, Clear), colour swatch,
"More..." and clear in both colour dialects, clear formatting, link insert
/ edit / replace text / remove / Ctrl+K, image / video / formula insert and
cancel, undo / redo enabling, read-only, find bar, icons drawn in the
palette's text colour for light and dark palettes.

### Deviations

- **Icons are painted, not SVG files**: a `QIconEngine` draws each glyph
  with `QPainter` in the palette's text colour, so icons follow dark
  palettes and there is no icon licence to track.
- **Quill.js shape** is a File menu toggle instead of a checkbox inside the
  save dialog (native dialogs cannot host extra widgets).
- **`small` is hidden by default**, as in flutter_quill.
- **Embeds are inserted inline at the cursor** (replacing the selection);
  placing block embeds on their own line is left to M7.
- **`StyleOptions::dark()`** added for dark palettes; `QuillEditor`
  always keeps `codeBlockBackgroundOnBlocks` off, whatever style it is given.
- **Collapsed "clear formatting"** clears the typing format only; with a
  selection it is `removeFormat` (Quill semantics, line formats included).
- **Exit criterion not checked here**: re-opening saved files in the
  flutter_quill example app needs a machine with Flutter (still manual).

### Findings

- During `destroyed()` a `QPointer<QuillEditor>` is still set while the
  object is already only a `QWidget`; UBSan flags any use. Toolbar and find
  bar drop their connections and pointer without touching the editor.
- No vertical spacing around headings and paragraphs yet (flutter_quill has
  header and paragraph spacing); noted for M8 fidelity.
