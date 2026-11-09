# M7 — Embeds beyond images

Estimate: 2–3 days. Depends on: M5 (can start after M3 for the rendering
part).

## Goal

Render and round-trip every embed type flutter_quill produces (`image`,
`video`, `formula`, `custom`) plus unknown app-defined keys, through a
registry that lets a host application supply its own renderer per embed key
or per flutter_quill custom type, and load remote images asynchronously.

## Deliverables

```
src/widgets/include/quill/
  EmbedRegistry.h    registry + EmbedHandler interface
  Embeds.h           built-in handlers: ImageEmbed, VideoEmbed, FormulaEmbed, CustomEmbed, GenericEmbed
src/widgets/
  EmbedRegistry.cpp ImageEmbed.cpp VideoEmbed.cpp FormulaEmbed.cpp CustomEmbed.cpp GenericEmbed.cpp
  ImageLoader.cpp    QNetworkAccessManager-based loader with cache
tests/widgets/tst_embeds.cpp
tests/fixtures/documents/embeds_*.json
```

## Design

### Representation (unchanged from M3/M4)

An embed is one `QChar::ObjectReplacementCharacter` whose char format
carries `prop::EmbedType` (key), `prop::EmbedData` (payload, any JSON) and
`prop::EmbedAttributes` (op attributes such as `width`, `height`, `style`,
`link`), plus `QTextFormat::ObjectType` set to a per-handler object type id
so `QAbstractTextDocumentLayout` dispatches drawing to the handler. Images
keep using `QTextImageFormat` because Qt's layout handles them natively, but
also carry the three properties so the exporter has one code path.

### Interface

```cpp
class EmbedHandler : public QObject, public QTextObjectInterface {
public:
  virtual QString key() const = 0;                               // "video", "formula", "custom", ...
  virtual bool accepts(const QJsonValue& data) const { return true; }   // for "custom": inspect inner type
  virtual QTextCharFormat makeFormat(const QJsonValue& data, const AttributeMap& attrs,
                                     QTextDocument* doc) const;        // default: ObjectReplacement format with props
  virtual QJsonValue toJson(const QTextCharFormat&) const;        // default: EmbedData property
  // QTextObjectInterface:
  QSizeF intrinsicSize(QTextDocument*, int posInDocument, const QTextFormat&) override = 0;
  void drawObject(QPainter*, const QRectF&, QTextDocument*, int, const QTextFormat&) override = 0;
  virtual bool handleClick(QuillEditor*, const QTextFormat&, QMouseEvent*) { return false; }
};

class EmbedRegistry : public QObject {
public:
  void registerHandler(std::unique_ptr<EmbedHandler>);           // later registrations win for same key
  EmbedHandler* handlerFor(const QString& key, const QJsonValue& data) const;  // first whose accepts() is true, else Generic
  void attachTo(QTextDocument*);                                 // documentLayout()->registerHandler(objectType, h) for all
};
```

`QuillEditor::embeds()` returns the editor's registry (one per editor,
pre-populated with the built-ins). `DeltaDocument::load/insert` take the
registry via `LoadOptions::embeds`; when null, the generic placeholder from
M3 is used (keeps `QuillQt::Widgets` usable without an editor).

### Built-in handlers

| key | data | rendering | interaction |
|---|---|---|---|
| image | URL string | native `QTextImageFormat`; `width`/`height` attrs (strings, `px` or bare) applied; `style` parsed for `width:`/`height:`; Qt loads `file:`/`data:`/`qrc:` itself; `http(s)` through `ImageLoader`: insert a placeholder-sized grey box, fetch, `doc->addResource(ImageResource, url, QImage)`, then `markContentsDirty` for relayout. In-memory cache keyed by URL, bounded. Host can set a custom `QNetworkAccessManager` (auth headers). | double-click emits `embedActivated(type, data)` |
| video | URL string | 16:9 box (max width = viewport − margins, honour `width`/`height`), dark background, play triangle, host text of the URL; optional poster via `attrs["poster"]` or a host-provided thumbnail callback | click emits `embedActivated`; host opens a player |
| formula | TeX string | bordered inline box, monospace source text (no TeX rendering in v1); `FormulaEmbed::setRenderer(std::function<QImage(QString)>)` lets a host plug in a renderer (e.g. a KaTeX web view or MicroTeX) | click emits `embedActivated` |
| custom | JSON **string** `{"type": "...", "data": ...}` (flutter_quill `CustomBlockEmbed`) | parse the string; look up a sub-handler registered with `CustomEmbed::registerType(type, handler)`; fallback is a chip `[type]`; the string is stored verbatim in `EmbedData` so export is byte-identical even if it is not valid JSON | sub-handler decides |
| anything else | any JSON | `GenericEmbed`: chip with the key name; payload preserved | emits `embedActivated` |

flutter_quill's `BlockEmbed` types are "block" embeds in its UI (own line)
but in the Delta they are still inline length-1 inserts, typically alone on
a line. Rendering them inline in Qt is correct for the data; the demo's
toolbar inserts them on their own line like flutter_quill does.

### Export

`DeltaDocument::toDelta` already reads `EmbedType/EmbedData/EmbedAttributes`.
This milestone adds: when a handler is registered, call `toJson(format)` so a
handler can normalise (e.g. image width changed by a resize handle becomes a
new `width` attribute).

### Resizing (optional, time permitting)

Image resize handles on selection, writing `width`/`height` attributes as
strings in flutter_quill style (`"300"`), emitting a change Delta via
`formatText` on the embed's index.

## Notes from M3

- M3 stores the whole embed object in `prop::Embed` (QJsonObject) and its
  key in `prop::EmbedType`; non-image embeds use object type
  `prop::EmbedObjectType` and currently take no space.
- Missing or not-yet-loaded images show Qt's file icon stretched to the
  requested size; replace it with a neutral placeholder box.
- flutter_quill renders image and video embeds as blocks
  (`display: block` in the example document). Decide between a block
  layout for embeds alone on their line and inline rendering otherwise.

## Notes from M5

- Embeds are inserted and restyled through `detail::embedCharFormat()`
  (`src/widgets/DocumentEdit.cpp`), shared by load and apply; registry
  dispatch belongs there.
- Typed text after an embed gets a sanitised typing format
  (`QuillEditorPrivate::sanitizeTypingFormat`); keep that when embeds get
  their own object types.
- Pasted images go through `QuillEditor::imageSourceFor()` (virtual).

## Notes from M6

- The toolbar's image / video / formula actions already exist
  (`QuillToolbar::insertEmbed`, prompts are virtual: image returns a file
  URL). They insert inline at the cursor; switch them to own-line block
  insertion here if that is the chosen layout.
- `quill-demo --sample embeds.json` and `flutter_quill_example.json` show the
  current placeholders; `ci/screenshot.sh` captures both palettes.

## Tasks

- [x] `EmbedHandler`, `EmbedRegistry`, attach to document layout; generic
      handler replaces the M3 placeholder.
- [x] `ImageLoader` with QNetworkAccessManager, cache, redirects, error
      image; tests with a local `QTcpServer`-based HTTP stub.
- [x] Video, formula, custom handlers; `CustomEmbed::registerType`.
- [x] `QuillEditor::embedActivated(const QString& type, const QJsonValue& data)`
      signal and click routing (`handleClick`).
- [x] Toolbar insert actions (M6) switched to `insertEmbed` with the right
      keys; "Insert custom" in the demo with a JSON editor for testing.
- [x] Fixtures: `embeds_video.json`, `embeds_formula.json`,
      `embeds_custom_flutter_quill.json` (real `custom` string payloads),
      `embeds_unknown.json`.
- [x] `tst_embeds`: load → render to QImage (non-blank at embed rect) →
      export equals input; registry precedence; custom sub-type dispatch;
      remote image load completion updates layout (spy on
      `documentLayoutChanged`).

## Exit criteria

- All embed fixtures round-trip exactly, including `custom` strings with
  nested JSON and non-ASCII text.
- Remote images display in the demo; a failing URL shows a broken-image
  box and does not block the UI.

## Risks

- `QTextImageFormat` and custom `QTextObjectInterface` differ in how
  `intrinsicSize` interacts with line height; test very tall embeds.
- flutter_quill versions differ in whether `custom` data is a JSON string or
  an object; `accepts()` handles both, export keeps the original form.

## Outcome (2026-11-09)

Done. Debug, Release and ASan/UBSan builds clean; 16 ctest entries green
(`tst_embeds` added, 23 cases); long fuzz runs (3000 API steps per
document, 20 000 key events) without drift.

- Public `EmbedRegistry.h` (`EmbedRenderer`, `EmbedContext`,
  `CustomEmbedRenderer`, `EmbedRegistry`) and `Embeds.h` (`ImageEmbed`,
  `VideoEmbed`, `FormulaEmbed`, `GenericEmbed`). One dispatcher object is
  registered on the document layout for `prop::EmbedObjectType` *and*
  `QTextFormat::ImageObject`, so images are drawn by `ImageEmbed` too
  (natural size, width/height/style attributes with aspect ratio, shrunk to
  the line, placeholder while loading, broken-image box when missing; this
  replaces Qt's stretched file icon).
- `DeltaDocument::load` attaches `LoadOptions::embeds` or
  `EmbedRegistry::shared()` (built-ins, no network); `apply()` attaches the
  shared one to documents that have none. `QuillEditor::embeds()` is the
  editor's own registry with remote images on.
- Remote images: `ImageLoader` (private) on a `QNetworkAccessManager`
  (host-replaceable), redirects (no less safe), 30 s timeout, 64 MiB
  per-download cap, bounded cache (default 64 MiB), failures remembered;
  on arrival the image becomes a document resource and only the fragments
  showing it are relaid out (`markContentsDirty`, no `contentsChange`, so no
  change Delta). Tested against a local `QTcpServer` stub (200, 404,
  redirect, request outliving its document, cache hit, shared registry not
  fetching).
- Video: 16:9 frame with play button and URL, 640 px max alone on its line,
  320 px inline, width/height/style honoured, optional thumbnail provider.
  Formula: monospace TeX box, `FormulaEmbed::setRenderer()` for real
  rendering. Unknown keys and custom types without a renderer: a chip with
  the key or custom type.
- Inline embeds other than images use vertical alignment `AlignMiddle` so
  they centre on the text; the exporter ignores that alignment, and an
  explicit `script` attribute still wins.
- `QuillEditor`: `embedActivated(type, data, index)` on click (images:
  double click) unless `EmbedRenderer::activate()` handles it, `embedAt()`,
  pointing-hand cursor and per-renderer tooltips, `insertBlockEmbed()`
  (flutter_quill's InsertEmbedsRule). The toolbar inserts image and video
  (renderers with `isBlock()`) on their own line, formulas inline.
- Demo: a `NotesEmbed` custom renderer for flutter_quill's example "notes"
  type (card with the note's text, click to edit), Edit > Insert custom
  embed (type + JSON data), embed activations shown in the status bar, the
  four new fixtures as samples.
- Fixtures: `embeds_video.json`, `embeds_formula.json`,
  `embeds_custom_flutter_quill.json` (string payloads with nested Delta
  JSON, non-ASCII and emoji, object payload, invalid JSON),
  `embeds_unknown.json`. All round-trip byte-identically through
  `QTextDocument` and `QuillEditor`.

### Deviations

- **flutter_quill's custom payload is `{"<type>": data}`** (CustomBlockEmbed
  `toJsonString`), not `{"type": ..., "data": ...}` as the plan said.
  `CustomEmbedRenderer::parse()` takes the string or object form;
  `encode()` writes the string form.
- **`EmbedRenderer`, not `EmbedHandler`**: `quill::EmbedHandler` already is
  the Delta core's embed OT hook (M2). Renderers are plain classes
  (`size()`, `paint()`, `activate()`), not `QTextObjectInterface`s, and one
  dispatcher serves all keys instead of an object type per handler.
- **No `makeFormat()` / `toJson()` hooks**: formats stay a pure function of
  the Delta (one code path for load, apply and export), and nothing
  normalises on export since image resizing was not done.
- **`insertEmbed()` keeps Quill's semantics** (inline at the index);
  own-line insertion is the separate `insertBlockEmbed()`.
- **Embeds sharing a line with text stay inline** with that text, like
  flutter_quill's `WidgetSpan`s; an embed alone on its line is a block.
- **Image resize handles** (optional) not done.
- **Tests stay offline**: `testutil::makeEditor` turns remote images off;
  `tests/lsan.supp` suppresses libproxy's one-time allocations that the
  first network request causes.
