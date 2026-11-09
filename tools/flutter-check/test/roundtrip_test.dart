// flutter_quill must load every quill-qt fixture and export without loss
// (the M6 exit criterion: files saved by quill-qt reopen in flutter_quill).
// flutter_quill's own load-time normalisations are listed, with the reason,
// in known_differences.json; a listed fixture must still differ. Fixtures
// that flutter_quill cannot render (listed under "render") are skipped: a
// render failure leaves the widget tree broken for the following tests.
import 'dart:convert';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_quill/flutter_quill.dart';
import 'package:flutter_test/flutter_test.dart';

import 'fixtures.dart';

class GenericEmbedBuilder extends EmbedBuilder {
  const GenericEmbedBuilder();
  @override
  String get key => 'generic';
  @override
  bool get expanded => false;
  @override
  Widget build(BuildContext context, EmbedContext embedContext) =>
      Text('[${embedContext.node.value.type}]');
}

void main() {
  final known = jsonDecode(File('test/known_differences.json').readAsStringSync()) as Map<String, dynamic>;
  final knownRoundTrip = (known['roundTrip'] as Map).cast<String, String>();
  final knownRender = (known['render'] as Map).cast<String, String>();

  for (final file in documentFixtures()) {
    final name = relativeName(file);
    test('round trip $name', () {
      final ops = readOps(file);
      final exported = Document.fromJson(ops).toDelta().toJson();
      final same = jsonEncode(canonical(exported)) == jsonEncode(canonical(ops));
      if (knownRoundTrip.containsKey(name)) {
        expect(same, isFalse, reason: '$name is listed in known_differences.json but round-trips now');
      } else {
        expect(canonical(exported), canonical(ops));
      }
    });

    testWidgets('renders $name', skip: knownRender.containsKey(name), (tester) async {
      final controller = QuillController(
        document: Document.fromJson(readOps(file)),
        selection: const TextSelection.collapsed(offset: 0),
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
            config: const QuillEditorConfig(unknownEmbedBuilder: GenericEmbedBuilder()),
          ),
        ),
      ));
      await tester.pump();
      expect(tester.takeException(), isNull);
      controller.dispose();
    });
  }
}
