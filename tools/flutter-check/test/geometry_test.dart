// Records where flutter_quill places each line of a document, for
// tst_flutter_parity::geometry (quill-qt must leave the same gaps between
// lines).
//
// For every line, the content box of its RenderEditableTextLine (the
// "body" child, i.e. without the line's and block's padding) is written as
// {top, height} in logical pixels, at a fixed editor width. Line heights
// depend on the font (flutter tests use a square test font), so quill-qt
// compares the gaps between consecutive lines, which come only from
// spacing and padding. Lines that hold only an embed are marked
// ("embed": "sized" when the attributes give its width and height, else
// "unsized"): their height is the embed's, not the font's, so quill-qt
// compares the distance to the next line for sized ones and skips unsized
// ones (flutter_quill_extensions' natural image size needs the image).
//
// Embeds use SizedEmbedBuilder: a box of the size flutter_quill_extensions'
// image builder would use (width/height from the attributes, the "style"
// declaration winning) and no padding. flutter_quill_extensions 11.0.0 never
// applies its "margin" style property (getElementAttributes parses the
// literal string 'margin'), and its video builder defaults the margin to 0,
// so a box without padding is what users see. The real builders are not
// used: they need network images and the video_player plugin.
//
// With WRITE_FIXTURES=1 the files in tests/fixtures/flutter_geometry are
// (re)written; otherwise the test fails when they are out of date.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_quill/flutter_quill.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fixtures.dart';

const double editorWidth = 800;
final bool write = Platform.environment['WRITE_FIXTURES'] == '1';
final Directory outDir = Directory('${fixturesDir.path}/flutter_geometry');

/// Documents whose geometry is recorded (relative to tests/fixtures).
const probed = [
  'documents/spacing_matrix.json',
  'documents/headers.json',
  'documents/lists_nested.json',
  'documents/blockquote_code.json',
  'documents/checklist.json',
  'documents/align_rtl_indent.json',
  'documents/basic_inline.json',
  'flutter_quill/generated/block_formats.json',
  'quillqt_exports/keyboard.json',
  'quillqt_exports/toolbar.json',
];

double? cssLength(String? v) {
  if (v == null) return null;
  final m = RegExp(r'^\s*([0-9]*\.?[0-9]+)\s*(px)?\s*$').firstMatch(v);
  return m == null ? null : double.parse(m.group(1)!);
}

Map<String, String> parseCss(String css) {
  final out = <String, String>{};
  for (final decl in css.split(';')) {
    final i = decl.indexOf(':');
    if (i > 0) out[decl.substring(0, i).trim()] = decl.substring(i + 1).trim();
  }
  return out;
}

class SizedEmbedBuilder extends EmbedBuilder {
  const SizedEmbedBuilder();
  @override
  String get key => 'sized';
  @override
  bool get expanded => false;
  @override
  Widget build(BuildContext context, EmbedContext embedContext) {
    final attrs = embedContext.node.style.attributes;
    var w = cssLength(attrs[Attribute.width.key]?.value?.toString());
    var h = cssLength(attrs[Attribute.height.key]?.value?.toString());
    final style = attrs['style']?.value;
    if (style is String) {
      final css = parseCss(style);
      w = cssLength(css['width']) ?? w;
      h = cssLength(css['height']) ?? h;
    }
    return SizedBox(width: w ?? 100, height: h ?? 60);
  }
}

/// The content boxes of all RenderEditableTextLines, in document order.
List<Rect> lineBodies(WidgetTester tester) {
  final out = <Rect>[];
  void visit(RenderObject o) {
    if (o.runtimeType.toString() == 'RenderEditableTextLine') {
      for (final child in o.debugDescribeChildren()) {
        final value = child.value;
        if (child.name == 'body' && value is RenderBox) {
          out.add(value.localToGlobal(Offset.zero) & value.size);
        }
      }
      return;
    }
    o.visitChildren(visit);
  }

  visit(tester.renderObject(find.byType(QuillEditor)));
  return out;
}

double round2(double v) => (v * 100).roundToDouble() / 100;

/// "sized" / "unsized" for a line holding only an embed, else null.
String? embedKind(Line line) {
  if (line.childCount != 1 || line.children.first is! Embed) return null;
  final attrs = (line.children.first as Embed).style.attributes;
  var w = cssLength(attrs[Attribute.width.key]?.value?.toString());
  var h = cssLength(attrs[Attribute.height.key]?.value?.toString());
  final style = attrs['style']?.value;
  if (style is String) {
    final css = parseCss(style);
    w = cssLength(css['width']) ?? w;
    h = cssLength(css['height']) ?? h;
  }
  return w != null && h != null ? 'sized' : 'unsized';
}

List<Line> documentLines(Document doc) => [
      for (final node in doc.root.children)
        if (node is Line) node else for (final child in (node as Block).children) child as Line,
    ];

void main() {
  for (final name in probed) {
    testWidgets('geometry $name', (tester) async {
      tester.view.physicalSize = const Size(editorWidth, 20000);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final file = File('${fixturesDir.path}/$name');
      final controller = QuillController(
        document: Document.fromJson(readOps(file)),
        selection: const TextSelection.collapsed(offset: 0),
        readOnly: true,
      );
      await tester.pumpWidget(MaterialApp(
        localizationsDelegates: const [
          GlobalMaterialLocalizations.delegate,
          GlobalWidgetsLocalizations.delegate,
          FlutterQuillLocalizations.delegate,
        ],
        home: Scaffold(
          body: QuillEditor.basic(
            controller: controller,
            config: const QuillEditorConfig(
              padding: EdgeInsets.zero,
              embedBuilders: [],
              unknownEmbedBuilder: SizedEmbedBuilder(),
            ),
          ),
        ),
      ));
      await tester.pump();
      expect(tester.takeException(), isNull);

      final bodies = lineBodies(tester);
      final lines = documentLines(controller.document);
      expect(bodies.length, lines.length);
      final origin = bodies.isEmpty ? 0.0 : bodies.first.top;
      final json = {
        'source': name,
        'width': editorWidth,
        'lines': [
          for (var i = 0; i < bodies.length; ++i)
            {
              'top': round2(bodies[i].top - origin),
              'height': round2(bodies[i].height),
              if (embedKind(lines[i]) != null) 'embed': embedKind(lines[i]),
            },
        ],
      };
      final out = File('${outDir.path}/${name.replaceAll('/', '__')}');
      final text = '${const JsonEncoder.withIndent(' ').convert(json)}\n';
      if (write) {
        out.parent.createSync(recursive: true);
        out.writeAsStringSync(text);
      } else {
        expect(out.existsSync(), isTrue, reason: '${out.path} missing; run with WRITE_FIXTURES=1');
        expect(jsonDecode(out.readAsStringSync()), jsonDecode(text));
      }
      controller.dispose();
    });
  }
}
