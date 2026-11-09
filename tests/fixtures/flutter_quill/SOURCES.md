# flutter_quill fixtures: provenance

| File | Source | Notes |
|---|---|---|
| `example_default_sample.json` | `example/lib/quill_delta_sample.dart` (`kQuillDefaultSample`) in https://github.com/singerdmx/flutter-quill at commit `e880534f8750072c879fecd72ada72d0902bc5e8` | Dart literal converted to JSON. The asset constant `kScreenshot2` was replaced with the string `assets/images/screenshot_2.webp`. Covers headers, ordered/bullet/checked lists, indent 1–3, blockquote, code-block, link, size, font, align, token, image with width/height/style, video embeds. |

| `generated/*.json` | Built with flutter_quill 11.6.0's `QuillController` API by `tools/flutter-check/test/export_test.dart` (headless `flutter test`, Flutter 3.47.9) | `inline_formats` (every inline toolbar attribute), `block_formats` (headers, lists, indent, quote, code, align, RTL, line height), `embeds` (image with style, video, formula, custom notes). |
| `scripts.json` | Same test: editing scripts run in a real flutter_quill 11.6.0 `QuillEditor` widget | Steps and the resulting document and selection; replayed by `tests/widgets/tst_flutter_parity.cpp`. |

flutter_quill is MIT licensed. Only 11.6.0 (the current release, which
needs Flutter 3.44+) is recorded; older majors do not resolve with the
Flutter SDK used here.
