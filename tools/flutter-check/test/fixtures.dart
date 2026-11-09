// Shared helpers: fixture discovery and canonical Delta JSON.
import 'dart:convert';
import 'dart:io';

import 'package:flutter_quill/quill_delta.dart';

final Directory fixturesDir = Directory('../../tests/fixtures');

/// Document fixtures flutter_quill must load: quill-qt's own fixtures, the
/// flutter_quill samples, and documents exported by quill-qt's editor.
List<File> documentFixtures() {
  final files = <File>[];
  for (final sub in ['documents', 'flutter_quill', 'quillqt_exports']) {
    final dir = Directory('${fixturesDir.path}/$sub');
    if (!dir.existsSync()) continue;
    files.addAll(dir
        .listSync(recursive: true)
        .whereType<File>()
        .where((f) => f.path.endsWith('.json') && !f.path.endsWith('/scripts.json')));
  }
  files.sort((a, b) => a.path.compareTo(b.path));
  return files;
}

String relativeName(File f) => f.path.substring(fixturesDir.path.length + 1);

/// The ops of a fixture (bare array or Quill.js {"ops": [...]}).
List<dynamic> readOps(File f) {
  final json = jsonDecode(f.readAsStringSync());
  return json is Map ? json['ops'] as List : json as List;
}

/// Ops merged the way Delta.push merges them, as JSON.
List<Map<String, dynamic>> canonical(List<dynamic> ops) {
  final delta = Delta();
  for (final op in Delta.fromJson(ops).toList()) {
    delta.push(op);
  }
  return delta.toJson();
}
