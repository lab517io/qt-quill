# quill-qt

A native Qt Widgets rich text editor that reads, writes and emits changes in
the [Quill Delta](https://quilljs.com/docs/delta) format, compatible with
[flutter_quill](https://pub.dev/packages/flutter_quill) and
[Quill](https://github.com/slab/quill).

**Status:** milestones M1 (skeleton), M2 (Delta core), M3 (Delta → QTextDocument), M4 (QTextDocument → Delta), M5 (editor widget), M6 (toolbar, demo), M7 (embeds), M8 (fidelity harness, CI) and M9a (portability groundwork on Linux) done; M9 (native Windows and macOS) waits for those machines. See
[PLAN.md](PLAN.md) for the design and [plans/](plans/README.md) for the
milestone plans.

## Motivation

The goal is a working native QtWidgets Quill editor, ported from
flutter_quill and Quill (the ancestor of the idea). Both work with the same
internal format, the Delta. Linux comes first, then the other main desktop
platforms.

## Requirements

- Qt 6.4 or newer (Core, Gui, Widgets, Network; Test for the test-suite)
- CMake 3.21+, a C++17 compiler

On Ubuntu 24.04: `ci/install-deps-ubuntu.sh`.

## Using it in your project (vendored)

Copy or submodule this tree, then:

```cmake
add_subdirectory(third_party/quill-qt)
target_link_libraries(myapp PRIVATE QuillQt::Widgets)   # or QuillQt::Delta for the GUI-free core
```

When vendored, the demo and tests are not built unless you set
`QUILLQT_BUILD_DEMO=ON` or `QUILLQT_BUILD_TESTS=ON`. Libraries are static and
there are no install rules.

| Target | Contents | Qt modules |
|---|---|---|
| `QuillQt::Delta` | Delta model and operational transform | Core |
| `QuillQt::Widgets` | editor widget, toolbar, converters | Core, Gui, Widgets, Network |

## Delta core

`QuillQt::Delta` is a C++ port of [slab/delta](https://github.com/slab/delta)
5.1 with identical semantics, checked against the upstream test-suite and
fast-diff outputs (see [tools/oracle](tools/oracle/README.md)).

```cpp
#include <quill/Delta.h>

using quill::Delta;

Delta doc = *Delta::fromJson(QByteArray(R"([{"insert":"Hello\n"}])"));
Delta change = Delta().retain(5, {{"bold", true}});
Delta result = doc.compose(change);   // [{"insert":"Hello","attributes":{"bold":true}},{"insert":"\n"}]
Delta undo = change.invert(doc);      // [{"retain":5,"attributes":{"bold":null}}]
Delta mine = change.transform(Delta().insert("Hi "), /*priority*/ true);
QByteArray json = result.toJson();    // flutter_quill shape; JsonShape::OpsObject for Quill.js
```

Lengths and indices are UTF-16 code units, like JavaScript and
`QTextDocument`.

## Converting between Delta and QTextDocument

```cpp
#include <quill/DeltaDocument.h>

QTextEdit edit;
quill::LoadResult r = quill::DeltaDocument::load(edit.document(), delta);
for (const QString& w : r.warnings)   // unknown attributes etc., preserved anyway
    qWarning() << w;

// ... edit with QTextEdit or QTextCursor ...

quill::Delta out = quill::DeltaDocument::toDelta(edit.document());
QByteArray json = out.toJson();       // flutter_quill shape
```

Export keeps unknown attributes and original value spellings, and derives
attributes from edits made with plain Qt APIs (bold, colours, sizes, links,
headings, lists, alignment, images).

Styling follows flutter_quill's defaults, scaled to the document's font;
see `quill::StyleOptions` to change sizes, colours, indent width or the
colour dialect (flutter_quill or CSS).

## The editor widget

```cpp
#include <quill/QuillEditor.h>

quill::QuillEditor editor;
quill::QuillToolbar toolbar(&editor);         // optional, flutter_quill's default controls
editor.setContents(delta);
QObject::connect(&editor, &quill::QuillEditor::textChange,
                 [](const quill::Delta& change, const quill::Delta& old, quill::Source source) {
                     // send `change` to collaborators, save, ...
                 });
editor.formatText(0, 5, {{"bold", true}});
editor.formatLine(0, 1, {{"list", "ordered"}});
editor.updateContents(remoteChange);          // Quill change Delta
quill::Delta doc = editor.getContents();
```

The API follows Quill's (`getContents`, `setContents`, `updateContents`,
`insertText`, `insertEmbed`, `deleteText`, `format`, `formatText`,
`formatLine`, `removeFormat`, `getFormat`, `getSelection`, `setSelection`).
Every edit, whether typed, pasted, undone or made through the API, emits one
`textChange` with a Quill-shaped change Delta and its source (`Api`, `User`,
`Silent`). Keyboard behaviour follows flutter_quill (checked against it, see
below): lists continue on Enter and end on an empty item, quotes and code
blocks end on an empty last line, Tab at the start of a list item indents,
Backspace at a line start merges the lines keeping the upper line's format,
`1. ` / `- ` / `[] ` start lists, Ctrl+B/I/U/Shift+X toggle formats.
`setEditingRules(QuillEditor::EditingRules::QuillJs)` selects Quill.js's
Backspace (outdent, then remove the block format) and checklist rules. `DeltaDocument::apply()` applies a
change Delta to a plain `QTextDocument` without the widget.

## Embeds

Images, videos, formulas, flutter_quill `custom` embeds and unknown keys are
drawn by renderers in an `EmbedRegistry`; the embed object and its
attributes always export unchanged. Remote images load in the background in
`QuillEditor`.

```cpp
#include <quill/EmbedRegistry.h>
#include <quill/Embeds.h>

class PollEmbed : public quill::CustomEmbedRenderer {   // {"custom": "{\"poll\": ...}"}
public:
    PollEmbed() : CustomEmbedRenderer("poll") {}
    QSizeF size(const quill::EmbedContext& c) const override { return {c.availableWidth, 80}; }
    void paint(QPainter* p, const QRectF& r, const quill::EmbedContext& c) const override { /* ... */ }
};

editor.embeds()->emplace<PollEmbed>();
editor.embeds()->find<quill::FormulaEmbed>()->setRenderer(renderTex);   // optional TeX renderer
editor.embeds()->setNetworkAccessManager(&authenticatedManager);        // optional
QObject::connect(&editor, &quill::QuillEditor::embedActivated,
                 [](const QString& type, const QJsonValue& data, int index) { /* play video, ... */ });
editor.insertBlockEmbed(index, "video", url);   // on its own line, as flutter_quill does
```

## Demo

![quill-demo](docs/screenshot.png)

`quill-demo` puts `QuillToolbar` (the flutter_quill default toolbar:
history, font and size, inline formats, colours, headings, lists, alignment,
quote and code blocks, links, images, video, formulas, search) on a
`QuillEditor`, with a dock showing the live document Delta and the stream of
change Deltas. It opens and saves flutter_quill JSON (or Quill.js
`{"ops": [...]}`) and has a dark palette.

```sh
build/debug/apps/demo/quill-demo [file.json] [--sample showcase.json] [--dark] [--log]
ci/screenshot.sh       # one PNG per embedded sample, light and dark
```

## Compatibility checks

- **Delta core vs quill-delta:** 4074 operation cases (hand-written plus 500
  seeded random scenarios) evaluated by the pinned npm package and replayed
  in C++ (`tst_oracle`), plus slab/delta's own test-suite (`tst_slab_recorded`).
  flutter_quill's `dart_quill_delta` is compared on the same cases.
- **flutter_quill itself** (`ci/check-flutter.sh`, headless): every fixture and
  every document quill-qt's editor exports loads in flutter_quill 11.6 without
  loss and renders; flutter_quill's own exports load in quill-qt without loss;
  21 editing scripts recorded in a real flutter_quill editor (Enter, Backspace,
  Tab, toolbar formats) give the same documents in `QuillEditor`
  (`tst_flutter_parity`).
- Where flutter_quill and Quill.js keyboards differ, `QuillEditor` follows
  flutter_quill; `setEditingRules(QuillEditor::EditingRules::QuillJs)`
  switches to Quill.js's rules.

Things flutter_quill does that quill-qt does not copy, found by these checks:
it moves inline videos onto their own line when loading, cannot render
invalid attribute values (`"header": 7`) or `custom` embeds whose value or
inner data is not a JSON string, and cannot lay out text with lone
surrogates. quill-qt writes custom embeds as strings
(`CustomEmbedRenderer::encode`).

## Platform notes

| | Linux | Windows | macOS |
|---|---|---|---|
| Status | supported, CI | cross-built and tested under Wine (llvm-mingw); MSVC CI job prepared, not yet run | CI job prepared, not yet run |
| Qt | 6.4 (Ubuntu 24.04) to 6.12; CI also tests 6.8 LTS | 6.8 LTS | 6.8 LTS |

- **Fonts:** Quill's generic names (`monospace`, `serif`, `sans-serif`) map to
  a style hint plus families found on each platform (Consolas / Menlo /
  DejaVu Sans Mono, Times New Roman / Times, Arial / Helvetica); code uses the
  system fixed-width font. Documents keep the generic name.
- **Shortcuts:** Ctrl+B/I/U, Ctrl+K, Ctrl+Shift+X and Ctrl+click on links are
  Cmd on macOS (Qt maps Control to Command).
- **Images:** plain file paths as flutter_quill stores them load on every
  platform (`C:\Users\me\pic.png`, `\\server\share\pic.png`, `/home/me/pic.png`);
  the document keeps the path as written.
- **Clipboard:** HTML from Word, LibreOffice, Google Docs, browsers and
  Windows CF_HTML is normalised (lists, quotes, code, no fonts or colours).
- **Deployment** (not yet verified): `windeployqt quill-demo.exe`,
  `macdeployqt quill-demo.app`.

## Building and testing

```sh
ci/build.sh            # Debug and Release into build/debug, build/release
                       # also: Asan, Clang, Qt-6.8.3 (via ci/install-qt.sh), Mingw (Windows cross-build)
ci/test.sh             # ctest with QT_QPA_PLATFORM=offscreen, no display needed (Mingw: under Wine)
ci/build.ps1, test.ps1 # Windows / MSVC equivalents
ci/check-oracle.sh     # regenerate the OT oracle (Node, optional Dart) and fail on drift
ci/check-flutter.sh    # flutter_quill compatibility (Flutter)
ci/format-check.sh     # clang-format-18 (--fix to format)
build/debug/apps/demo/quill-demo --sample showcase.json
```

## Layout

```
src/delta/     QuillQt::Delta sources, public headers in include/quill/
src/widgets/   QuillQt::Widgets sources, public headers in include/quill/
apps/demo/     quill-demo
tests/         QtTest suites and fixtures
ci/            provider-neutral build and test scripts
tools/oracle/  Node.js scripts that record reference behaviour into tests/fixtures
tools/dart-oracle/    the same OT cases on dart_quill_delta
tools/flutter-check/  flutter_quill compatibility tests (Flutter)
.github/workflows/    CI: build/test (Debug, Release, ASan+UBSan), oracle, flutter_quill, screenshots, format
plans/         milestone plans
```
