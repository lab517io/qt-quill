# M3 — Delta → `QTextDocument`

Estimate: 2 days. Depends on: M2.

## Goal

Load any flutter_quill or Quill.js document Delta into a `QTextDocument` so
that it renders correctly in `QTextEdit`, with every attribute either mapped
to a Qt format or preserved as an opaque property so M4 can write it back
unchanged.

## Deliverables

```
src/widgets/include/quill/
  DeltaDocument.h     static load(QTextDocument*, const Delta&, const LoadOptions&) and
                      insert(QTextCursor&, const Delta&) for sub-range insertion (used by M5)
  Formats.h           property ids, attribute<->format mapping, style tables
  Colors.h            CSS colour parse/format
src/widgets/
  DeltaDocument.cpp Formats.cpp Colors.cpp
tests/widgets/
  tst_load.cpp        structure assertions per fixture
tests/fixtures/documents/*.json
```

## Design

### Property ids (Formats.h)

```cpp
namespace quill::prop {
  // QTextFormat::UserProperty = 0x100000; leave room for consumers above 0x100100.
  constexpr int InlineAttributes = QTextFormat::UserProperty + 0x10;  // QJsonObject: original inline attrs
  constexpr int BlockAttributes  = QTextFormat::UserProperty + 0x11;  // QJsonObject: original line attrs
  constexpr int BlockKind        = QTextFormat::UserProperty + 0x12;  // int: Paragraph, Header, Blockquote, CodeBlock, ListItem
  constexpr int EmbedType        = QTextFormat::UserProperty + 0x13;  // QString: image, video, formula, custom, ...
  constexpr int EmbedData        = QTextFormat::UserProperty + 0x14;  // QJsonValue: the embed payload
  constexpr int EmbedAttributes  = QTextFormat::UserProperty + 0x15;  // QJsonObject: width/height/style/...
  constexpr int ObjectType       = QTextFormat::UserObject + 1;       // custom text object type (M7)
}
```

Storing the original attribute JSON alongside the derived Qt format is the
mechanism that makes round-trip lossless: M4 derives canonical attributes
from the Qt format and merges keys it cannot derive from the stored JSON.

### Algorithm

```
load(doc, delta):
  doc->clear(); cursor at start; disable undo (doc->setUndoRedoEnabled(false)) during load
  ensure delta ends with "\n" (append if not; flutter_quill asserts this, be lenient)
  listStack = []           // open QTextList per indent level for numbering continuity
  delta.eachLine([&](line, lineAttrs, index) {
      blockFormat, blockCharFormat = blockFormatsFor(lineAttrs)
      if index > 0: cursor.insertBlock(blockFormat, blockCharFormat) else cursor.setBlockFormat/ setBlockCharFormat
      attach to list (see Lists) or clear listStack when the line is not a list item
      for each op in line.ops():
          if text: cursor.insertText(text, charFormatFor(op.attributes, lineAttrs))
          else:    insertEmbed(cursor, op.embed, op.attributes)   // M3: image + generic placeholder; M7: registry
  })
  re-enable undo; doc->setModified(false)
```

### Inline attributes → `QTextCharFormat`

| key | mapping |
|---|---|
| bold | `setFontWeight(QFont::Bold)` |
| italic | `setFontItalic(true)` |
| underline | `setFontUnderline(true)` |
| strike | `setFontStrikeOut(true)` |
| code | `setFontFamilies({monospace fallback})`, `setFontFixedPitch(true)`, light grey background, `setProperty(InlineAttributes)` keeps `code:true` |
| script | `sub` → `AlignSubScript`, `super` → `AlignSuperScript` |
| link | `setAnchor(true)`, `setAnchorHref(url)`, link colour + underline |
| color | `Colors::parse` → `setForeground` |
| background | `Colors::parse` → `setBackground` |
| font | `setFontFamilies({value})`; Quill names `serif`, `monospace`, `sans-serif` map to `QFont::StyleHint` generic families |
| size | flutter_quill: `small`/`large`/`huge` → 0.75/1.5/2.5 × base point size (Quill CSS); numeric string (e.g. `"14"`, `"18.0"`) → that many points (flutter_quill treats it as logical pixels, close enough on desktop); anything else → ignored visually, preserved |
| small | 0.8 × base point size |
| placeholder, token, anything unknown | no visual effect; preserved in `InlineAttributes` |

Base point size comes from `doc->defaultFont()`. All inline formats in a
header line additionally get the header size adjustment (see below) so
headers look like headers even with mixed inline styles.

### Line attributes → `QTextBlockFormat` (+ block char format)

| key | mapping |
|---|---|
| header 1–6 | `setHeadingLevel(n)`; block char format and every fragment get `setProperty(FontSizeAdjustment, 4 - n)` and bold, matching what Qt's HTML importer does for `<h1>`..`<h6>` |
| list | see Lists |
| indent n | list item: encoded in the `QTextList` indent (n + 1); other blocks: `setIndent(n)` |
| align | left/center/right/justify → `Qt::AlignLeft/AlignHCenter/AlignRight/AlignJustify`; combine with `Qt::AlignAbsolute` so RTL does not flip explicit left/right (Quill treats them as absolute) |
| direction rtl | `setLayoutDirection(Qt::RightToLeft)`; with no `align`, Quill right-aligns RTL lines: do the same |
| blockquote | `BlockKind = Blockquote`, `setLeftMargin(16)`, text colour muted; decoration painted by M5 |
| code-block | `BlockKind = CodeBlock`, block char format monospace, `setBackground(light grey)`, `setNonBreakableLines(false)`; accept `true` or a language string, preserved in `BlockAttributes` |
| line-height | flutter_quill values `1.0`, `1.15`, `1.5`, `2.0` → `setLineHeight(value * 100, QTextBlockFormat::ProportionalHeight)` |
| unknown | preserved in `BlockAttributes` |

Every block stores the full original line attributes in `BlockAttributes`.

### Lists

Quill lines carry `list` + `indent` individually; Qt groups blocks in
`QTextList` objects. To reproduce Quill's numbering (a nested sub-list does
not reset the parent's numbering, a different list type at the same level
does) maintain a stack indexed by indent level:

```
onListLine(type, indent):
  listStack.resize(indent + 1)                  // deeper levels close
  if listStack[indent] exists and listStack[indent].type == type: list->add(block)
  else: listStack[indent] = cursor.createList(formatFor(type, indent))
onNonListLine(): listStack.clear()
```

`QTextListFormat` per (type, indent): `bullet` → `ListDisc`/`ListCircle`/
`ListSquare` cycling by indent, `ordered` → `ListDecimal`/`ListLowerAlpha`/
`ListLowerRoman` cycling by indent (Quill's CSS does exactly this), `checked`
and `unchecked` → `ListDisc` with `QTextBlockFormat::setMarker(Checked/
Unchecked)`. `setIndent(indent + 1)`.

### Embeds in M3

- `image`: `QTextImageFormat` with `setName(url)`; `width`/`height` from the
  op attributes (flutter_quill writes them as strings, possibly in `style`
  as `width: 100px; height: 80px;` — parse both). Local files and `data:`
  URLs load immediately via `doc->addResource`; `http(s)` loading is M7.
  Store `EmbedType = "image"`, `EmbedData`, `EmbedAttributes`.
- Everything else: insert `QChar::ObjectReplacementCharacter` with a char
  format carrying `EmbedType`/`EmbedData`/`EmbedAttributes` and no text
  object yet (renders as a blank glyph). M7 replaces this with real
  rendering through the same properties, so M4's exporter is unaffected.

### Colours (Colors.h)

`parse(QString) -> std::optional<QColor>`: `#rgb`, `#rrggbb`, `#rrggbbaa`,
named colours (`QColor::fromString`), `rgb()`/`rgba()`/`hsl()`/`hsla()` with
ints, floats and percentages. `format(QColor, const QString& original)`:
return `original` if it parses to the same colour, else `#rrggbb` (or
`rgba(...)` when alpha < 255). flutter_quill writes `#FF000000`-style ARGB
hex in some versions (`Color.value` as 8 hex digits with alpha first):
detect by the flutter_quill fixture and handle 8-digit hex as ARGB when the
document came from flutter_quill (option in `LoadOptions`, default on, since
flutter_quill is first).

### LoadOptions

```cpp
struct LoadOptions {
  QFont baseFont;                        // defaults to doc->defaultFont()
  bool argbHexColors = true;             // flutter_quill 8-digit hex is AARRGGBB
  QHash<QString, double> namedSizes = {{"small",0.75},{"large",1.5},{"huge",2.5}};
  bool strict = false;                   // throw on unknown/invalid ops instead of preserving
};
```

## Tasks

- [ ] `Formats.h`: property ids, `charFormatFor`, `blockFormatsFor`,
      list format tables.
- [ ] `Colors.cpp` + `tst_colors` (parse/format cases incl. ARGB hex).
- [ ] `DeltaDocument::load` with eachLine walk, list stack, header sizing.
- [ ] Image embeds (local/data URLs), generic placeholder for others.
- [ ] Fixtures: `tests/fixtures/documents/` with
      `basic_inline.json`, `headers.json`, `lists_nested.json`,
      `checklist.json`, `blockquote_code.json`, `align_rtl_indent.json`,
      `sizes_fonts_colors.json` (flutter_quill size and colour forms),
      `line_height.json`, `embeds.json`, `flutter_quill_sample_*.json`
      (real exports from the flutter_quill example app, bare-array shape).
- [ ] `tst_load`: per fixture assert block count, each block's `BlockKind`,
      heading level, list membership and numbering (`QTextList::itemNumber`),
      marker type, selected char formats at known offsets, image presence.
- [ ] Render each fixture to `QImage` offscreen (no assertion beyond
      non-blank) to catch layout crashes.

## Tests

- `tst_colors`, `tst_load` as above.
- Numbering test: fixture `1. a / 2. b / (indent 1) a. c / 3. d` must give
  item numbers 1, 2, 1, 3.

## Exit criteria

- All fixtures load without warnings in strict mode, except intentionally
  unknown attributes in `unknown_attributes.json`, which must load in lenient
  mode and keep their JSON.
- Visual check of the rendered PNGs of each fixture (saved under
  `build/*/test-output/`) looks like the Quill playground for the same Delta.

## Risks

- Qt heading sizing is not automatic; forgetting `FontSizeAdjustment` on
  fragments makes mixed-style headers look wrong. Covered by the
  `headers.json` test.
- flutter_quill colour and size value forms vary across versions; the fixture
  set should include at least 9.x and 10.x/11.x exports.
