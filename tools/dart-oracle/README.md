# Dart oracle

Evaluates the OT cases of `tests/fixtures/ot/cases/` with
[dart_quill_delta](https://pub.dev/packages/dart_quill_delta), the Delta
implementation flutter_quill uses, and writes
`tests/fixtures/ot/expected-dart/`. `tests/delta/tst_oracle.cpp`
(`dartPortAgrees`) compares these results with quill-delta's; the C++ port
itself is checked against quill-delta.

```sh
dart pub get
dart run bin/oracle.dart
```

dart_quill_delta has no `eachLine`, `length` or `changeLength` (its `length`
is the op count) and no cursor hint for `diff`; those cases are written as
unsupported. Results count as agreeing when identical, identical once
canonicalised (Dart does not always merge adjacent inserts or put an insert
before a delete), or, for `invert`, when both undo the change to the same
document (Dart omits null removals of attributes the text never had).
Remaining divergences are listed with their reason in
`tests/fixtures/ot/dart-divergences.json`: currently one, Dart's diff splits
an emoji's surrogate pair (diff_match_patch works on code units).
