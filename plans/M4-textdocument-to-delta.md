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

- [ ] `inlineAttributes()` with derive-then-overlay rule.
- [ ] `lineAttributes()` with list/marker/heading mapping and conflict rules.
- [ ] `toDelta(doc)` and `toDelta(doc, from, to)` sharing one walker.
- [ ] Normalisation of soft breaks, frames, tables, empty document.
- [ ] `tst_roundtrip`: for each fixture in `tests/fixtures/documents/`,
      `toDelta(load(d)) == d` (Delta equality, which is JSON deep equality
      after `push` merging; fixtures are stored pre-normalised with merged
      runs so equality is exact).
- [ ] `tst_export_edits`: build documents using only `QTextCursor` calls a
      user action would make (merge char format bold, `createList`,
      `setHeadingLevel`, insert image, Shift+Enter soft break) and assert the
      expected Delta JSON literally.
- [ ] `tst_normalize`: table/frame flattening, soft break splitting.
- [ ] Sub-range export tests: `toDelta(doc, a, b) == toDelta(doc).slice(a, b)`
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
