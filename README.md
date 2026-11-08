# quill-qt

A native Qt Widgets rich text editor that reads, writes and emits changes in
the [Quill Delta](https://quilljs.com/docs/delta) format, compatible with
[flutter_quill](https://pub.dev/packages/flutter_quill) and
[Quill](https://github.com/slab/quill).

**Status:** milestones M1 (skeleton), M2 (Delta core), M3 (Delta → QTextDocument), M4 (QTextDocument → Delta) and M5 (editor widget) done. See
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
`Silent`). Keyboard behaviour follows Quill and flutter_quill: lists continue
on Enter and end on an empty item, Tab indents list items, Backspace at a
line start outdents or removes the block format, `1. ` / `- ` / `[] ` start
lists, Ctrl+B/I/U/Shift+X toggle formats. `DeltaDocument::apply()` applies a
change Delta to a plain `QTextDocument` without the widget.

## Building and testing

```sh
ci/build.sh            # Debug and Release into build/debug, build/release
ci/test.sh             # ctest with QT_QPA_PLATFORM=offscreen, no display needed
build/debug/apps/demo/quill-demo tests/fixtures/flutter_quill/example_default_sample.json --log
```

## Layout

```
src/delta/     QuillQt::Delta sources, public headers in include/quill/
src/widgets/   QuillQt::Widgets sources, public headers in include/quill/
apps/demo/     quill-demo
tests/         QtTest suites and fixtures
ci/            provider-neutral build and test scripts
tools/oracle/  Node.js scripts that record reference behaviour into tests/fixtures
plans/         milestone plans
```
