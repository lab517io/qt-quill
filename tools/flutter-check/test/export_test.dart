// Records what flutter_quill itself produces, for quill-qt's test-suite:
//
// * tests/fixtures/flutter_quill/generated/*.json: documents built with
//   flutter_quill's controller API (every toolbar attribute and embed).
// * tests/fixtures/flutter_quill/scripts.json: editing scripts (typing,
//   Enter, Backspace, Tab, formatting) run in a real QuillEditor widget, with
//   the resulting document. tests/widgets/tst_flutter_parity.cpp replays the
//   same steps in QuillEditor and compares.
//
// With WRITE_FIXTURES=1 the files are (re)written; otherwise the test fails
// when flutter_quill's output differs from the committed files.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_quill/flutter_quill.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fixtures.dart';
import 'roundtrip_test.dart' show GenericEmbedBuilder;

final bool write = Platform.environment['WRITE_FIXTURES'] == '1';
final Directory outDir = Directory('${fixturesDir.path}/flutter_quill');

String pretty(Object json) => '${const JsonEncoder.withIndent(' ').convert(json)}\n';

void record(String relative, Object json) {
  final file = File('${outDir.path}/$relative');
  if (write) {
    file.parent.createSync(recursive: true);
    file.writeAsStringSync(pretty(json));
  } else {
    expect(file.existsSync(), isTrue, reason: '${file.path} missing; run with WRITE_FIXTURES=1');
    expect(jsonDecode(file.readAsStringSync()), jsonDecode(jsonEncode(json)));
  }
}

// --- Documents built with the controller API ------------------------------------

typedef Build = void Function(QuillController c);

void type(QuillController c, String text) {
  final s = c.selection;
  c.replaceText(s.start, s.end - s.start, text, TextSelection.collapsed(offset: s.start + text.length));
}

void line(QuillController c, String text, [List<Attribute> lineAttrs = const []]) {
  final start = c.selection.start;
  type(c, text);
  for (final a in lineAttrs) {
    c.formatText(start, text.length, a);
  }
  type(c, '\n');
  // A new line starts without the previous line's block formats.
  for (final a in lineAttrs) {
    if (a.scope == AttributeScope.block) c.formatSelection(Attribute.clone(a, null));
  }
}

void styled(QuillController c, String text, List<Attribute> attrs) {
  final start = c.selection.start;
  type(c, text);
  for (final a in attrs) {
    c.formatText(start, text.length, a);
  }
  c.updateSelection(TextSelection.collapsed(offset: start + text.length), ChangeSource.local);
  for (final a in attrs) {
    c.formatSelection(Attribute.clone(a, null));
  }
}

final Map<String, Build> documents = {
  'inline_formats': (c) {
    styled(c, 'bold', [Attribute.bold]);
    type(c, ' ');
    styled(c, 'italic', [Attribute.italic]);
    type(c, ' ');
    styled(c, 'underline', [Attribute.underline]);
    type(c, ' ');
    styled(c, 'strike', [Attribute.strikeThrough]);
    type(c, ' ');
    styled(c, 'code', [Attribute.inlineCode]);
    type(c, ' H');
    styled(c, '2', [Attribute.subscript]);
    type(c, 'O x');
    styled(c, '2', [Attribute.superscript]);
    type(c, ' ');
    styled(c, 'small', [Attribute.small]);
    type(c, '\n');
    styled(c, 'red', [const ColorAttribute('#FFF44336')]);
    type(c, ' ');
    styled(c, 'on yellow', [const BackgroundAttribute('#FFFFEB3B')]);
    type(c, ' ');
    styled(c, 'mono', [const FontAttribute('monospace')]);
    type(c, ' ');
    styled(c, 'serif', [const FontAttribute('serif')]);
    type(c, '\n');
    styled(c, 'small', [const SizeAttribute('small')]);
    type(c, ' ');
    styled(c, 'large', [const SizeAttribute('large')]);
    type(c, ' ');
    styled(c, 'huge', [const SizeAttribute('huge')]);
    type(c, ' ');
    styled(c, 'twenty', [const SizeAttribute('20')]);
    type(c, ' ');
    styled(c, 'link', [const LinkAttribute('https://pub.dev/packages/flutter_quill')]);
    type(c, ' ');
    styled(c, 'all', [Attribute.bold, Attribute.italic, Attribute.underline, const ColorAttribute('#FF2196F3')]);
    type(c, '\n');
  },
  'block_formats': (c) {
    for (var level = 1; level <= 6; level++) {
      line(c, 'Heading $level', [HeaderAttribute(level: level)]);
    }
    line(c, 'Bullet', [Attribute.ul]);
    line(c, 'Bullet nested', [Attribute.ul, Attribute.indentL1]);
    line(c, 'Ordered', [Attribute.ol]);
    line(c, 'Ordered nested', [Attribute.ol, Attribute.indentL2]);
    line(c, 'Checked', [Attribute.checked]);
    line(c, 'Unchecked', [Attribute.unchecked]);
    line(c, 'Quote', [Attribute.blockQuote]);
    line(c, 'var code = true;', [Attribute.codeBlock]);
    line(c, 'Left', [Attribute.leftAlignment]);
    line(c, 'Center', [Attribute.centerAlignment]);
    line(c, 'Right', [Attribute.rightAlignment]);
    line(c, 'Justify', [Attribute.justifyAlignment]);
    line(c, 'مرحبا', [Attribute.rtl, Attribute.rightAlignment]);
    line(c, 'Indented', [Attribute.indentL3]);
    line(c, 'Line height', [LineHeightAttribute.lineHeightOneAndHalf]);
  },
  'embeds': (c) {
    type(c, 'Image\n');
    c.replaceText(c.selection.start, 0, BlockEmbed.image('https://flutter.github.io/assets-for-api-docs/assets/widgets/owl.jpg'),
        TextSelection.collapsed(offset: c.selection.start + 1));
    c.formatText(c.selection.start - 1, 1, const StyleAttribute('width: 300px; height: 200px;'));
    type(c, '\nVideo\n');
    c.replaceText(c.selection.start, 0, BlockEmbed.video('https://www.youtube.com/watch?v=V4hgdKhIqtc'),
        TextSelection.collapsed(offset: c.selection.start + 1));
    type(c, '\nFormula ');
    c.replaceText(c.selection.start, 0, BlockEmbed.formula(r'e^{i\pi}+1=0'),
        TextSelection.collapsed(offset: c.selection.start + 1));
    type(c, '\n');
    final note = jsonEncode([
      {'insert': 'A note — ünïcode 🎉\n'},
    ]);
    c.replaceText(c.selection.start, 0, BlockEmbed.custom(CustomBlockEmbed('notes', note)),
        TextSelection.collapsed(offset: c.selection.start + 1));
    type(c, '\nEnd\n');
  },
};

// --- Editing scripts ---------------------------------------------------------------

/// Step kinds: {"type": text}, {"key": "enter"|"backspace"|"tab"|"shift+tab"},
/// {"select": [index, length]}, {"format": {key: value}} (QuillController.
/// formatSelection, i.e. the toolbar).
final Map<String, List<Map<String, dynamic>>> scripts = {
  'bold_typing': [
    {'type': 'a'}, {'format': {'bold': true}}, {'type': 'bc'}, {'format': {'bold': null}}, {'type': 'd'},
  ],
  'bullet_list_continue_and_exit': [
    {'type': 'one'}, {'format': {'list': 'bullet'}}, {'key': 'enter'}, {'type': 'two'},
    {'key': 'enter'}, {'key': 'enter'}, {'type': 'after'},
  ],
  'ordered_list_continue_and_exit': [
    {'type': 'one'}, {'format': {'list': 'ordered'}}, {'key': 'enter'}, {'type': 'two'},
    {'key': 'enter'}, {'key': 'enter'}, {'type': 'after'},
  ],
  'checked_item_continues_unchecked': [
    {'type': 'done'}, {'format': {'list': 'checked'}}, {'key': 'enter'}, {'type': 'next'},
  ],
  'header_enter_at_end': [
    {'type': 'Title'}, {'format': {'header': 1}}, {'key': 'enter'}, {'type': 'body'},
  ],
  'header_enter_in_middle': [
    {'type': 'Title'}, {'format': {'header': 2}}, {'select': [2, 0]}, {'key': 'enter'},
  ],
  'blockquote_enter_twice_exits': [
    {'type': 'q'}, {'format': {'blockquote': true}}, {'key': 'enter'}, {'type': 'r'},
    {'key': 'enter'}, {'key': 'enter'}, {'type': 'out'},
  ],
  'code_block_enter_twice_exits': [
    {'type': 'a = 1'}, {'format': {'code-block': true}}, {'key': 'enter'}, {'type': 'b = 2'},
    {'key': 'enter'}, {'key': 'enter'}, {'type': 'out'},
  ],
  'backspace_at_list_item_start': [
    {'type': 'a'}, {'format': {'list': 'bullet'}}, {'key': 'enter'}, {'type': 'b'},
    {'select': [2, 0]}, {'key': 'backspace'},
  ],
  'backspace_in_empty_list_item': [
    {'type': 'a'}, {'format': {'list': 'bullet'}}, {'key': 'enter'}, {'key': 'backspace'},
  ],
  'backspace_merges_into_header': [
    {'type': 'a'}, {'format': {'header': 2}}, {'key': 'enter'}, {'type': 'b'},
    {'select': [2, 0]}, {'key': 'backspace'},
  ],
  'backspace_merges_header_into_paragraph': [
    {'type': 'a'}, {'key': 'enter'}, {'type': 'b'}, {'format': {'header': 2}},
    {'select': [2, 0]}, {'key': 'backspace'},
  ],
  'tab_in_empty_list_item_indents': [
    {'type': 'a'}, {'format': {'list': 'bullet'}}, {'key': 'enter'}, {'key': 'tab'}, {'type': 'b'},
  ],
  'tab_at_list_item_start_indents': [
    {'type': 'ab'}, {'format': {'list': 'bullet'}}, {'select': [0, 0]}, {'key': 'tab'},
  ],
  'tab_inside_list_item': [
    {'type': 'ab'}, {'format': {'list': 'bullet'}}, {'select': [1, 0]}, {'key': 'tab'},
  ],
  'shift_tab_outdents': [
    {'type': 'a'}, {'format': {'list': 'bullet'}}, {'format': {'indent': 2}}, {'select': [0, 0]},
    {'key': 'shift+tab'},
  ],
  'tab_in_paragraph': [
    {'type': 'ab'}, {'select': [1, 0]}, {'key': 'tab'},
  ],
  'link_does_not_extend_at_end': [
    {'type': 'x'}, {'select': [0, 1]}, {'format': {'link': 'https://a.b'}}, {'select': [1, 0]}, {'type': 'y'},
  ],
  'inline_format_over_enter': [
    {'format': {'italic': true}}, {'type': 'ab'}, {'key': 'enter'}, {'type': 'c'},
  ],
  'format_selection_across_lines': [
    {'type': 'one'}, {'key': 'enter'}, {'type': 'two'}, {'select': [1, 5]},
    {'format': {'bold': true}}, {'format': {'list': 'ordered'}},
  ],
  'delete_selection_across_lines': [
    {'type': 'one'}, {'format': {'header': 1}}, {'key': 'enter'}, {'type': 'two'},
    {'format': {'list': 'bullet'}}, {'select': [2, 3]}, {'key': 'backspace'},
  ],
};

Future<void> runStep(WidgetTester tester, QuillController c, Map<String, dynamic> step) async {
  if (step.containsKey('type')) {
    type(c, step['type'] as String);
  } else if (step.containsKey('select')) {
    final s = (step['select'] as List).cast<int>();
    c.updateSelection(TextSelection(baseOffset: s[0], extentOffset: s[0] + s[1]), ChangeSource.local);
  } else if (step.containsKey('format')) {
    (step['format'] as Map).forEach((key, value) {
      final base = Attribute.fromKeyValue(key as String, null) ?? Attribute(key, AttributeScope.inline, null);
      c.formatSelection(Attribute.clone(base, value));
    });
  } else if (step['key'] == 'enter') {
    type(c, '\n');
  } else if (step['key'] == 'backspace') {
    await tester.sendKeyEvent(LogicalKeyboardKey.backspace);
  } else if (step['key'] == 'tab') {
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
  } else if (step['key'] == 'shift+tab') {
    await tester.sendKeyDownEvent(LogicalKeyboardKey.shiftLeft);
    await tester.sendKeyEvent(LogicalKeyboardKey.tab);
    await tester.sendKeyUpEvent(LogicalKeyboardKey.shiftLeft);
  } else {
    throw ArgumentError('unknown step $step');
  }
  await tester.pump();
}

Future<QuillController> pumpEditor(WidgetTester tester) async {
  final controller = QuillController.basic();
  await tester.pumpWidget(MaterialApp(
    localizationsDelegates: const [
      GlobalMaterialLocalizations.delegate,
      GlobalWidgetsLocalizations.delegate,
      FlutterQuillLocalizations.delegate,
    ],
    home: Scaffold(
      body: QuillEditor.basic(
        controller: controller,
        config: const QuillEditorConfig(autoFocus: true, unknownEmbedBuilder: GenericEmbedBuilder()),
      ),
    ),
  ));
  await tester.pump();
  return controller;
}

/// Unmounts the editor so its cursor-blink timer does not outlive the test.
Future<void> finish(WidgetTester tester) async {
  await tester.pumpWidget(const SizedBox());
  await tester.pump(const Duration(seconds: 1));
}

void main() {
  documents.forEach((name, build) {
    testWidgets('document $name', (tester) async {
      final c = await pumpEditor(tester);
      build(c);
      await tester.pump();
      record('generated/$name.json', c.document.toDelta().toJson());
      await finish(tester);
    });
  });

  final results = <Map<String, dynamic>>[];
  scripts.forEach((name, steps) {
    testWidgets('script $name', (tester) async {
      final c = await pumpEditor(tester);
      for (final step in steps) {
        await runStep(tester, c, step);
      }
      results.add({
        'name': name,
        'steps': steps,
        'result': c.document.toDelta().toJson(),
        'selection': [c.selection.start, c.selection.end - c.selection.start],
      });
      await finish(tester);
    });
  });

  tearDownAll(() {
    results.sort((a, b) => (a['name'] as String).compareTo(b['name'] as String));
    record('scripts.json', {'flutter_quill': '11.6.0', 'scripts': results});
  });
}
