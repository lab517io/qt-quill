# M5 — `QuillEditor` widget

Estimate: 3 days. Depends on: M4.

## Goal

A `QTextEdit` subclass whose public API mirrors Quill's editor API
(`getContents`, `setContents`, `updateContents`, `format*`, `getFormat`,
`text-change` and `selection-change` signals with change Deltas and a
`source`), with the keyboard behaviour Quill and flutter_quill users expect
and the visual decorations Qt does not draw on its own.

## Deliverables

```
src/widgets/include/quill/
  QuillEditor.h   the widget
  Range.h         struct Range { int index; int length; }
  Source.h        enum class Source { Api, User, Silent }
src/widgets/
  QuillEditor.cpp QuillEditor_p.h          // d-pointer; keyboard handling, change tracking
  BlockDecorations.cpp                     // blockquote bar, code-block background
tests/widgets/
  tst_editor_api.cpp tst_editor_changes.cpp tst_editor_keys.cpp
```

## Design

### Public API

```cpp
class QuillEditor : public QTextEdit {
  Q_OBJECT
public:
  explicit QuillEditor(QWidget* parent = nullptr);

  // Content
  Delta getContents(int index = 0, int length = INT_MAX) const;
  Delta setContents(const Delta& doc, Source = Source::Api);        // returns change delta
  Delta updateContents(const Delta& change, Source = Source::Api);  // applies change via compose, returns it
  QString getText(int index = 0, int length = INT_MAX) const;
  int getLength() const;                                            // == document()->characterCount()
  Delta insertText(int index, const QString&, const AttributeMap& = {}, Source = Source::Api);
  Delta insertEmbed(int index, const QString& type, const QJsonValue& value, Source = Source::Api);
  Delta deleteText(int index, int length, Source = Source::Api);

  // Formatting (names and values are Quill attribute names and values)
  Delta format(const QString& name, const QJsonValue& value, Source = Source::User);        // current selection
  Delta formatText(int index, int length, const AttributeMap&, Source = Source::Api);
  Delta formatLine(int index, int length, const AttributeMap&, Source = Source::Api);
  Delta removeFormat(int index, int length, Source = Source::Api);
  AttributeMap getFormat(int index, int length) const;       // intersection of formats, like Quill
  AttributeMap getFormat() const;                            // current selection

  // Selection
  std::optional<Range> getSelection() const;
  void setSelection(int index, int length, Source = Source::Api);

  // Options
  void setReadOnlyFormats(const QStringList&);               // formats the toolbar must not offer
  EmbedRegistry* embeds();                                   // M7

signals:
  void textChanged(const quill::Delta& change, const quill::Delta& oldContents, quill::Source source);
  void selectionChanged(std::optional<quill::Range> range, std::optional<quill::Range> oldRange, quill::Source source);
  void editorChanged();                                      // either of the above
  void linkActivated(const QUrl&);
};
```

`QTextEdit::textChanged()` (no args) keeps working; the overload with
arguments is the Quill one. Note: `Q_SIGNAL` overloads require
`qOverload<>` for connections; document this in the header.

### Change Deltas

Keep `d->contents` (the current document Delta) in sync and derive every
change from it rather than re-exporting the whole document on each
keystroke:

```
connect(document(), &QTextDocument::contentsChange, [pos, removed, added]):
  if d->suppress: return                              // bulk load / setContents
  Delta newSlice = DeltaDocument::toDelta(document(), pos, pos + added);
  Delta oldSlice = d->contents.slice(pos, pos + removed);
  Delta change   = Delta().retain(pos).concat(oldSlice.diff(newSlice)).chop();   // retain(0) is dropped by push
  Delta old      = d->contents;
  d->contents    = d->contents.compose(change);
  Q_ASSERT_X(d->contents == DeltaDocument::toDelta(document()), "QuillEditor", "contents drift");  // debug builds only
  emit textChanged(change, old, d->currentSource);
```

Using `diff` on the slices gives Quill-shaped changes for every kind of Qt
edit: typing yields `retain/insert`, deleting yields `retain/delete`,
format changes (`removed == added`) yield `retain(n, {bold: true})`, and
paste or undo yield minimal mixed deltas. `d->currentSource` is `User` by
default and set to `Api` inside the API methods via an RAII guard; `Silent`
suppresses the signal but still updates `d->contents`.

Qt emits `contentsChange` once per `QTextCursor` edit block, so API methods
wrap their edits in `beginEditBlock/endEditBlock` to produce one change and
one undo step.

### API methods map to Qt cursors

- `insertText/insertEmbed/deleteText`: `QTextCursor` positioned by Delta
  index (identical to Qt position), apply `Formats::charFormatFor` for the
  attributes.
- `formatText`: `cursor.mergeCharFormat(deltaFormat)`; for `null` values
  build a format that clears the property (`QTextCharFormat::clearProperty`
  cannot be merged, so for removal rebuild the format of each fragment in
  the range: iterate fragments, compute `attributes::compose(current, change,
  false)`, apply `charFormatFor(result)` with `setCharFormat`).
- `formatLine`: for each block in range, compute new line attributes via
  `attributes::compose`, rebuild `blockFormatsFor`, re-attach to lists using
  the M3 list-stack logic applied locally (re-run list grouping for the
  affected blocks and their neighbours).
- `getFormat(range)`: intersection of inline attributes over all fragments
  plus intersection of line attributes over all blocks, same as Quill (a
  collapsed selection reports the format of the character before the cursor,
  or the block's char format at line start).
- `updateContents(change)`: apply op by op with a cursor at the running
  index: retain with attrs → `formatText/formatLine` split by whether the
  range contains `\n` (Quill applies line attributes in retains to the
  newline chars only); inserts → `insertText/insertEmbed`; deletes →
  `deleteText`. Single edit block.
- `setContents`: `suppress` on, `DeltaDocument::load`, `suppress` off,
  `d->contents = doc`, emit one change `old.diff(new)`.

### Notes from M3

- Implement `DeltaDocument::insert(QTextCursor&, const Delta&)` here
  (moved from M3) for paste and `updateContents`. Quill semantics: a
  `"\n"` with attributes A splits the current block; the part before the
  cursor gets A, the new block keeps the original line's format.
- After any edit that changes `list` or `indent`, call
  `DeltaDocument::regroupLists(first, last, style)` on the affected blocks;
  it extends itself to the surrounding list run.
- From M2: `transformPosition(i, true)` keeps a cursor before a concurrent
  insert at `i`.

### Notes from M4

- Editor API methods should write the new attributes into
  `prop::InlineAttributes` / `prop::BlockAttributes` and restyle with
  `formats::charFormat` / `formats::blockFormat`, so actual == expected and
  the export is exact. Plain `QTextEdit` actions (Ctrl+B, HTML paste) still
  export correctly through derivation, but only for the properties M4 can
  derive (not `code`, `blockquote`, `code-block`, `small`).
- Change Deltas from `contentsChange` can use
  `DeltaDocument::toDelta(doc, from, to)`; it equals `slice` of the full
  export (tested on random ranges).
- When a line's attributes change, re-run `formats::charFormat` on its
  text: inline styling depends on line attributes (header sizes, code-block
  font, quote colour).

### Keyboard behaviour (keyPressEvent)

| Key | Context | Behaviour (Quill default keyboard module) |
|---|---|---|
| Enter | list item with text | new block inherits `list`, `indent`; inline formats carried like Qt default |
| Enter | empty list item | remove `list` (and `indent`), stay on the line |
| Enter | header line | new line is a normal paragraph |
| Enter | blockquote | stays blockquote (Quill behaviour); Enter on empty blockquote line removes it |
| Enter | code-block | stays code-block; two consecutive empty code lines + Enter exits (Quill "code exit") |
| Shift+Enter | anywhere | inserts `\n` as a normal block (no soft break); same line attributes |
| Backspace | at line start, line has `list` or `indent` | if `indent` > 0 decrement, else remove `list`; do not merge lines |
| Backspace | at line start, header/blockquote/code-block | remove the block format first |
| Backspace | at line start, plain | Qt default (merge with previous line); the merged line keeps the previous line's attributes, like Quill |
| Tab / Shift+Tab | list item | indent +1 (max 8) / −1 |
| Tab | elsewhere | insert `\t` |
| Ctrl+B / I / U | selection or caret | toggle bold/italic/underline via `format()` |
| Ctrl+Shift+X | | toggle strike |
| Ctrl+K | | emit `linkRequested()` for the toolbar's link dialog |
| Space after `1. `, `- `, `[] ` at line start | plain line | autoformat to ordered/bullet/unchecked list (Quill has `list autofill`) |

`QTextEdit` default handling is kept for everything else (movement,
clipboard, IME).

### Mouse

- Checkbox toggle: on `mouseReleaseEvent`, if the click is on the marker
  area of a block with a marker (`cursorForPosition` + `blockBoundingRect`
  and `QTextListFormat` indent width), flip Checked/Unchecked through
  `formatLine`, so a change Delta `retain(n, {list: "checked"})` is emitted.
  **M1 finding:** Qt 6.4.2's `QTextEdit` already toggles the marker on
  click by itself (see `tst_smoke::probeBuiltInCheckboxToggle`). The
  resulting block format change still arrives through `contentsChange`, so
  the change Delta is emitted either way. Decide in the first task whether to
  keep Qt's toggle (and only adjust the hit area) or intercept the click in
  `mousePressEvent` to route it through `formatLine` with `Source::User`.
- Links: `anchorAt()` on `mouseReleaseEvent` with `Ctrl` held (or always in
  read-only mode) emits `linkActivated`. Pointing-hand cursor over anchors.

### Decorations (BlockDecorations.cpp)

In `paintEvent`, before calling `QTextEdit::paintEvent`, iterate visible
blocks: `BlockKind == Blockquote` → 4px vertical bar in the left margin;
`BlockKind == CodeBlock` → background rectangle covering adjacent code
blocks as one rounded panel (Quill renders consecutive code lines as one
block). The block background set in M3 is removed in favour of this to avoid
gaps between lines.

### Images and clipboard

- `canInsertFromMimeData`/`insertFromMimeData`: HTML paste goes through
  `QTextDocument` HTML import into a scratch document, then
  `toDelta(scratch)` with normalisation, then `updateContents`, so pasted
  content is always Quill-shaped. Image data pasted (`hasImage`) is inserted
  as a `data:` URL image embed; the host app can intercept with
  `imagePasted(QImage)` to upload and replace the URL.
- Drag and drop of files: image files become image embeds (file URL).

## Tasks

- [ ] Decide whether to keep Qt's built-in checkbox toggling (present in 6.4.2, see M1) or intercept it.
- [ ] Skeleton with d-pointer, `Source` guard, `contents` cache, change
      derivation, debug drift assertion.
- [ ] Content API (`getContents`, `setContents`, `updateContents`,
      `insertText/Embed`, `deleteText`, `getText`, `getLength`).
- [ ] Format API (`format`, `formatText`, `formatLine`, `removeFormat`,
      `getFormat`) incl. local list regrouping.
- [ ] Selection API and `selectionChanged` with old range.
- [ ] Keyboard table above; autoformat lists.
- [ ] Checkbox click, link click, cursor shape.
- [ ] Decorations in `paintEvent`.
- [ ] Clipboard HTML/image handling, drop handling.
- [ ] Undo/redo: confirm Qt undo steps produce correct change Deltas
      (they go through `contentsChange` too).

## Tests (offscreen)

- `tst_editor_api`: every API method against expected Deltas, mirroring
  Quill's own `test/unit/core/editor.ts` cases where applicable.
- `tst_editor_changes`: `QTest::keyClicks` typing, selection + Ctrl+B,
  Backspace/Delete, paste of HTML, undo; after each step assert the emitted
  change equals `old.diff(new)` and `editor.getContents() == toDelta(document())`.
- `tst_editor_keys`: each row of the keyboard table.
- Fuzz test: random sequences of key events on a seeded generator; after
  each event assert the drift invariant and `getLength() == characterCount()`.

## Exit criteria

- All tests pass; drift assertion never fires in the fuzz run (10k events).
- Manual session under `xvfb-run` of the M6 demo feels like Quill for lists,
  headers, quotes and code blocks.

## Risks

- `contentsChange` reporting for format-only changes on ranges spanning
  blocks: `removed == added` includes block separators; the slice diff
  handles it, but the undo-step grouping may emit two signals for one user
  action. Acceptable; Quill also emits per-mutation.
- Qt's `QTextEdit` list handling on Enter creates a new `QTextList` item
  automatically, which may conflict with the custom Enter handling; take
  over Enter entirely in list context.
