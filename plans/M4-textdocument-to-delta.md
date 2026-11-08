# M4 — `QTextDocument` → Delta, normalisation, round-trip

Estimate: 2 days. Depends on: M3.

## Goal

Export a `QTextDocument` (loaded by M3 or edited by a user) to a Delta that
flutter_quill and Quill.js accept, such that `toDelta(load(d)) == d` for every
fixture and that user edits made with plain Qt APIs (Ctrl+B, list buttons)
produce the attributes Quill would produce.

## Deliverables

```
src/widgets/include/quill/DeltaDocument.h   (extended)
  static Delta toDelta(const QTextDocument*, const ExportOptions& = {});
  static Delta toDelta(const QTextDocument*, int from, int to, ...);  // sub-range, used by M5 change deltas
  static AttributeMap inlineAttributes(const QTextCharFormat&, const ExportOptions&);
  static AttributeMap lineAttributes(const QTextBlock&, const ExportOptions&);
src/widgets/Export.cpp Normalize.cpp
tests/widgets/tst_roundtrip.cpp tst_export_edits.cpp tst_normalize.cpp
```

## Design

### Walk

```
toDelta(doc, from, to):
  for block in blocks intersecting [from, to):
     for fragment in block (QTextBlock::iterator), clipped to range:
        if fragment text is ObjectReplacementCharacter and format has EmbedType:
             delta.insert(embedJson(format), embedAttrs(format))          // one op per char
        else if fragment is an image (QTextImageFormat): delta.insert({"image": name}, attrs)
        else: for each run after normalising text (see below):
             delta.insert(text, inlineAttributes(charFormat))
     if block end (position after last char) is inside [from, to]:
        delta.insert("\n", lineAttributes(block))
  return delta   // Delta::push merges adjacent equal-attribute runs
```

Range semantics match Qt positions one-to-one (confirmed by M1 smoke test):
the final block's terminator corresponds to the Delta's trailing `\n`, so
`toDelta(doc)` always ends with `\n` and `toDelta(doc).length() ==
doc->characterCount()`.

### Note from M3: derive by comparing with the expected format

M3 made every char and block format a pure function of the attributes
(`formats::charFormat`, `formats::blockFormat`) and stored the original
attributes on it. The exporter should therefore start from the stored
attributes, recompute the expected Qt format from them, and derive an
attribute only for Qt properties where the actual format differs from the
expected one (a user edit). Loader styling (header sizes and weight, code
backgrounds, link and quote colours) then never leaks into the Delta, and the
rules below only run on edited properties. Text typed by the user inherits
the neighbouring format, including the stored attributes, so it exports
correctly with no extra work. The embed object is in `prop::Embed`.

### Deriving inline attributes

Derive the canonical Quill attributes from the Qt format first, then overlay
preserved keys:

```
derived = {}
bold      if fontWeight >= QFont::Bold  (and block is not a header, where bold is implied and must not be exported)
italic, underline, strike from the font flags
script    from verticalAlignment
link      from anchorHref when isAnchor
code      if property InlineAttributes has code:true, or (fontFixedPitch && !codeBlock line)
color     Colors::format(foreground, original string from InlineAttributes["color"]) when foreground is set and differs from the default
background likewise, excluding the automatic code/code-block background
font      families[0] mapped back (generic hint -> "serif"/"monospace"/"sans-serif"), original string if it resolves to the same family
size      if InlineAttributes has size and it maps to the current point size -> original string;
          else if point size differs from base -> named size when it equals 0.75/1.5/2.5 x base, otherwise numeric string (flutter_quill accepts "14")
small     from InlineAttributes only
for key in InlineAttributes not in known-set: derived[key] = stored value   // placeholder, token, app-specific
```

The rule "derived value wins, original string preferred when equivalent"
means a user who recolours bold text in the editor gets a new `color`, while
untouched text keeps `#FF0000FF` or `rgb(255,0,0)` exactly as it came in.

### Deriving line attributes

```
header    from headingLevel (1..6)
list      from textList: marker Checked/Unchecked -> checked/unchecked; else style decimal/alpha/roman -> ordered, disc/circle/square -> bullet
indent    list item: listFormat.indent - 1 (omit when 0); else blockFormat.indent (omit when 0)
align     from alignment, omit left (Quill default) unless direction is rtl and align is left (Quill stores it explicitly then)
direction rtl when layoutDirection == RightToLeft
blockquote, code-block from BlockKind; code-block value: original from BlockAttributes (true or language) else true
line-height when lineHeightType == Proportional and != 100 -> flutter_quill string form ("1.5", "1.15", "2.0", "1.0")
unknown keys from BlockAttributes overlay
```

Attributes that Quill forbids together are resolved the way Quill does:
`header` wins over `list`; `code-block` wins over `blockquote`.

### Normalisation (Normalize.cpp)

Applied inside the walk so it is also correct for sub-range exports:

- `QChar::LineSeparator` (U+2028, Shift+Enter) → `\n` carrying the same line
  attributes as the containing block. Exact for Quill, which has no soft
  breaks.
- `QChar::Nbsp` kept (Quill keeps it too); `QChar::ParagraphSeparator` never
  appears in fragment text but is guarded anyway.
- Tables and frames (`QTextFrame` children other than the root): blocks are
  visited in document order and exported as plain paragraphs; a
  `qWarning()` with the frame position is emitted once per export. The
  editor (M5) prevents creating them.
- Empty document → `[{"insert":"\n"}]`.
- Image inserted via plain Qt (`QTextImageFormat` without `EmbedType`):
  exported as `{"image": name}` plus `width`/`height` attributes when the
  format has them (flutter_quill style, as strings).

### ExportOptions

```cpp
struct ExportOptions {
  QFont baseFont;                 // size reference; defaults to doc->defaultFont()
  bool emitNumericSizes = true;   // false -> drop sizes that are not small/large/huge (Quill.js strict)
  bool argbHexColors = true;      // write 8-digit hex as AARRGGBB only when the original was in that form
};
```

## Tasks

- [x] `inlineAttributes()` with derive-then-overlay rule.
- [x] `lineAttributes()` with list/marker/heading mapping and conflict rules.
- [x] `toDelta(doc)` and `toDelta(doc, from, to)` sharing one walker.
- [x] Normalisation of soft breaks, frames, tables, empty document.
- [x] `tst_roundtrip`: for each fixture in `tests/fixtures/documents/`,
      `toDelta(load(d)) == d` (Delta equality, which is JSON deep equality
      after `push` merging; fixtures are stored pre-normalised with merged
      runs so equality is exact).
- [x] `tst_export_edits`: build documents using only `QTextCursor` calls a
      user action would make (merge char format bold, `createList`,
      `setHeadingLevel`, insert image, Shift+Enter soft break) and assert the
      expected Delta JSON literally.
- [x] `tst_normalize`: table/frame flattening, soft break splitting.
- [x] Sub-range export tests: `toDelta(doc, a, b) == toDelta(doc).slice(a, b)`
      for many random ranges on each fixture.

## Tests

As listed in Tasks. The sub-range equality test is the critical one for M5,
because change Deltas are built from sub-range exports.

## Exit criteria

- Round-trip equality for every fixture, including unknown attributes and
  flutter_quill colour/size forms.
- Sub-range export equals `slice` for all tested ranges.
- A document typed with Qt-only APIs exports to Deltas that the Quill.js
  playground renders identically (manual check, automated in M8).

## Risks

- `QTextList` style cycling by indent (M3) must be inverted exactly, or
  nested ordered lists export as `bullet`. Covered by `lists_nested.json`.
- Qt may split fragments at arbitrary points (e.g. after undo); exporter must
  never depend on fragment boundaries, only on formats. `push` merging makes
  the output canonical.

## Outcome (2026-11-08)

Done. Debug, Release and ASan/UBSan builds clean; 10 test executables green.
`tst_export` has 38 cases:

| Check | Coverage |
|---|---|
| Round trip `toDelta(load(d)) == d` | all 9 fixtures incl. the flutter_quill example, unknown attributes, embeds, custom style |
| Sub-range export equals `slice` | 300 random ranges per fixture |
| Fixed point after random Qt edits | 25 × 12 random `QTextCursor` edits per fixture; export → load → export must be identical (≥ 20 of 25 iterations must actually change the document) |
| Qt-API edits | inline flags, script, colours, link, font, sizes, removals, typing, heading/list/checklist/indent/RTL/line-height, Enter in a header, Shift+Enter, images, image resize, HTML import, tables |
| Performance | 5000 lines export in ~75 ms (Debug) |

### How derivation works (refines the plan)

- **Compare against the expected format.** Start from the stored
  attributes, compute the format they produce, derive an attribute only for
  Qt properties that differ. Loader styling never leaks; unknown attributes
  and original spellings (`"red"`, `"15.0"`) survive edits to the same text.
- **Line context of inline attributes.** Text in a block styled by quill-qt
  is compared against the line attributes stored on the block (what it was
  styled for), not the line as it is now; otherwise removing a header with
  Qt calls made the leftover header styling look like `bold` + `size`.
  Blocks Qt created itself (HTML import, `setPlainText`) have no stored
  attributes and use the current line.
- **Structural first.** `link` is derived before the other properties, and
  those are compared against a format that includes it, so a link's own
  underline and colour are not reported again.
- **`QTextList::remove()`** adds the list indent to the block indent; the
  exporter compensates (Quill keeps the indent level when a list is removed),
  and `regroupLists` now restores the line's indent after removal.
- **New values**: sizes are written as `"small"/"large"/"huge"` when they
  match, else a numeric string (`"14"`); colours as flutter_quill
  `#AARRGGBB` unless the original spelling still matches; resized images get
  `width`/`height` strings and those properties are removed from `style`
  (which would otherwise win on reload).

### Deviations

- `ExportOptions` holds only an optional `StyleOptions`. `load()` stores
  the style it used on the document (`documentStyle()`), so export matches
  the load without the caller passing anything. `emitNumericSizes` and
  `argbHexColors` were dropped; the colour dialect comes from the style.
- Not derived from Qt formats (only settable through attributes, i.e. the M5
  editor API): inline `code`, `blockquote`, `code-block`, `small`. Plain Qt
  has no unambiguous representation for them.
- Tables/frames are exported as plain lines with a warning; sub-range
  positions are not guaranteed to align inside them. The editor (M5) will not
  create them.
- Test JSON in `tst_export` uses single quotes in ordinary string literals:
  Qt 6.4's moc cannot tokenize raw strings containing escapes like `"\n"`
  and silently drops the test class.
