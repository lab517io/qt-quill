// Evaluates tests/fixtures/ot/cases/*.json with dart_quill_delta, the Delta
// implementation flutter_quill uses, and writes tests/fixtures/ot/expected-dart/.
// tests/delta/tst_oracle.cpp compares these results with quill-delta's.
//
//   dart run bin/oracle.dart
//
// dart_quill_delta has no eachLine / length / changeLength (its `length` is
// the op count); those cases are written as {"unsupported": true}.
import 'dart:convert';
import 'dart:io';

import 'package:dart_quill_delta/dart_quill_delta.dart';

Delta delta(dynamic ops) => Delta.fromJson((ops as List?) ?? const []);

Object? evaluate(Map<String, dynamic> c) {
  final a = delta(c['a']);
  final b = delta(c['b']);
  final priority = c['priority'] == true;
  switch (c['op']) {
    case 'compose':
      return a.compose(b).toJson();
    case 'transform':
      return a.transform(b, priority).toJson();
    case 'transformPosition':
      // quill-delta's `priority` keeps the position; Dart's `force` shifts it.
      return a.transformPosition((c['index'] as num).toInt(), force: !priority);
    case 'diff':
      if (c['cursor'] != null) return const _Unsupported(); // no cursor hint in Dart
      return a.diff(b).toJson();
    case 'invert':
      return a.invert(b).toJson();
    case 'slice':
      final end = c['end'];
      return a.slice((c['start'] as num?)?.toInt() ?? 0, end == null ? null : (end as num).toInt()).toJson();
    case 'concat':
      return a.concat(b).toJson();
    default:
      return const _Unsupported();
  }
}

class _Unsupported {
  const _Unsupported();
}

Map<String, Object?> run(Map<String, dynamic> c) {
  try {
    final result = evaluate(c);
    if (result is _Unsupported) return {'unsupported': true};
    return {'result': result};
  } catch (e) {
    return {'error': e.toString()};
  }
}

void main() {
  final root = Directory.fromUri(Platform.script.resolve('../../../tests/fixtures/ot/'));
  final out = Directory('${root.path}expected-dart')..createSync(recursive: true);
  final files = Directory('${root.path}cases')
      .listSync()
      .whereType<File>()
      .where((f) => f.path.endsWith('.json'))
      .toList()
    ..sort((x, y) => x.path.compareTo(y.path));
  for (final file in files) {
    final cases = (jsonDecode(file.readAsStringSync()) as List).cast<Map<String, dynamic>>();
    final lines = <String>[];
    for (final c in cases) {
      lines.add('${jsonEncode(c['name'])}: ${jsonEncode(run(c))}');
    }
    final name = file.uri.pathSegments.last;
    File('${out.path}/$name').writeAsStringSync('{\n${lines.join(',\n')}\n}\n');
    stdout.writeln('$name: ${cases.length} cases');
  }
}
