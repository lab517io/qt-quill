# flutter_quill compatibility check

A Flutter test package that runs flutter_quill (pinned in `pubspec.yaml`)
headless against quill-qt's fixtures. Run it with `ci/check-flutter.sh`
(needs Flutter; `FLUTTER=/path/to/flutter` if it is not on PATH).

| Test | Checks |
|---|---|
| `test/roundtrip_test.dart` | Every document in `tests/fixtures/documents`, `flutter_quill` and `quillqt_exports` (documents written by quill-qt's editor, see `tests/widgets/tst_flutter_parity.cpp`) loads in flutter_quill and exports unchanged (`Document.fromJson(...).toDelta()`), and renders in a `QuillEditor` without exceptions. |
| `test/geometry_test.dart` | Where flutter_quill places each line of a set of documents (`tests/fixtures/documents/spacing_matrix.json` and others), at 800 px width: the content box of every line, without spacing. `tst_flutter_parity::geometry` compares quill-qt's gaps between lines with these files (`tests/fixtures/flutter_geometry/`; `known_differences.json` there lists accepted differences by line). Embeds are drawn as boxes of their attribute size with no padding, which is what `flutter_quill_extensions` 11.0.0 does (its `margin` style property is never applied). |
| `test/export_test.dart` | flutter_quill's own output is unchanged: documents built with its controller API (`tests/fixtures/flutter_quill/generated/`) and editing scripts run in a real editor widget (`scripts.json`: typing, Enter, Backspace, Tab, toolbar formats, with the resulting document and selection). quill-qt replays the scripts in `tst_flutter_parity` and must produce the same documents. |

`test/known_differences.json` lists, with the reason, fixtures that
flutter_quill changes on load (`roundTrip`; such a fixture must still
differ, so the list stays accurate) or cannot render (`render`; skipped).

To record flutter_quill's output again (after changing the scripts, the
probed documents or the flutter_quill version), and quill-qt's exports:

```sh
WRITE_FIXTURES=1 ci/check-flutter.sh
QUILLQT_WRITE_FIXTURES=1 build/debug/tests/widgets/tst_flutter_parity
```
