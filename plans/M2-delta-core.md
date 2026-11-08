# M2 — `libquilldelta`: Delta core

Estimate: 2–3 days. Depends on: M1.

## Goal

A GUI-free C++17/Qt Core library implementing the Quill Delta model with the
exact semantics of `slab/delta` (the JS library both Quill.js and
flutter_quill's `dart_quill_delta` follow), including the operational
transform functions, JSON I/O in both flutter_quill and Quill.js shapes, and
a ported test-suite.

## Deliverables

```
src/delta/include/quill/
  Attributes.h      AttributeMap typedef + attribute algebra
  Op.h              Op value type
  OpIterator.h      cursor over ops with peek/next(length)
  Delta.h           Delta class
  Json.h            parse/serialize helpers, error type
src/delta/
  Attributes.cpp Op.cpp OpIterator.cpp Delta.cpp Json.cpp
  FastDiff.cpp/.h   string diff (port of fast-diff, Apache-2.0, attribution in file)
tests/delta/
  tst_attributes.cpp tst_delta_builder.cpp tst_delta_compose.cpp
  tst_delta_transform.cpp tst_delta_position.cpp tst_delta_diff.cpp
  tst_delta_invert.cpp tst_delta_helpers.cpp tst_json.cpp
THIRD_PARTY_NOTICES.md   slab/delta (BSD-3) test fixtures, fast-diff (Apache-2.0)
```

## Design

### Types

```cpp
namespace quill {

// null QJsonValue == "remove this attribute" marker, as in JS null.
using AttributeMap = QMap<QString, QJsonValue>;

namespace attributes {
  std::optional<AttributeMap> compose(const AttributeMap* a, const AttributeMap* b, bool keepNull = false);
  std::optional<AttributeMap> diff(const AttributeMap* a, const AttributeMap* b);
  AttributeMap invert(const AttributeMap* attr, const AttributeMap* base);
  std::optional<AttributeMap> transform(const AttributeMap* a, const AttributeMap* b, bool priority = false);
}

class Op {
public:
  enum class Type { Insert, Retain, Delete };
  static Op insertText(QString text, std::optional<AttributeMap> attrs = {});
  static Op insertEmbed(QJsonObject embed, std::optional<AttributeMap> attrs = {});   // exactly one key
  static Op retain(int length, std::optional<AttributeMap> attrs = {});
  static Op remove(int length);                                                       // 'delete' is a keyword
  Type type() const; int length() const; bool isEmbed() const;
  const QString& text() const; const QJsonObject& embed() const;
  const std::optional<AttributeMap>& attributes() const;
  bool operator==(const Op&) const;   // deep JSON equality for embeds and attributes
};

class Delta {
public:
  Delta(); explicit Delta(QList<Op> ops);
  Delta& insert(const QString&, std::optional<AttributeMap> = {});
  Delta& insert(const QJsonObject& embed, std::optional<AttributeMap> = {});
  Delta& retain(int, std::optional<AttributeMap> = {});
  Delta& remove(int);
  Delta& push(Op);                     // JS push(): merges adjacent compatible ops,
                                       // keeps inserts before a trailing delete
  Delta& chop();                       // drop trailing attribute-less retain
  int length() const; int changeLength() const;
  Delta slice(int start = 0, int end = INT_MAX) const;
  Delta concat(const Delta&) const;
  Delta compose(const Delta& other) const;
  Delta transform(const Delta& other, bool priority = false) const;
  int transformPosition(int index, bool priority = false) const;
  Delta invert(const Delta& base) const;
  Delta diff(const Delta& other, int cursorHint = -1) const;
  template <class F> void eachLine(F fn, const QString& newline = "\n") const;  // fn(Delta line, AttributeMap attrs, int index) -> bool
  const QList<Op>& ops() const;
  bool operator==(const Delta&) const;
};

class OpIterator {  // mirrors JS Iterator
public:
  explicit OpIterator(const QList<Op>&);
  bool hasNext() const; int peekLength() const; Op::Type peekType() const;
  Op next(int length = INT_MAX); Op peek() const; QList<Op> rest();
};

} // namespace quill
```

### Semantics to copy exactly (from slab/delta `src/Delta.ts`, `src/AttributeMap.ts`)

- `push`: if the last op is a delete and the new op is a delete, merge; if the
  last op is a delete and the new op is an insert, insert the new op *before*
  the delete (so inserts precede deletes); merge adjacent inserts of text with
  equal attributes; merge adjacent retains with equal attributes.
- `compose`: iterate both; when `other` is an insert push it; when `this` is a
  delete push it; otherwise take `min(peekLength)` of both: if `other` is a
  retain and `this` is retain/insert, result keeps `this` content with
  `attributes::compose(thisAttrs, otherAttrs, keepNull = thisOp.isRetain())`;
  if `other` is a delete and `this` is a retain, push the delete; if `this` is
  an insert and `other` deletes it, drop both. Fast path: when `other` starts
  with a plain retain and `this` starts with inserts, copy inserts directly.
  Finish with `chop()`.
- `transform(other, priority)`: inserts from `this` when (`this` is insert and
  (priority or `other` is not insert)) produce `retain(len)` in the result;
  `other`'s inserts are pushed as is; for overlapping retain/delete pairs: if
  `this` is delete, skip; if `other` is delete, push delete; else push
  `retain(len, attributes::transform(thisAttrs, otherAttrs, priority))`.
- `transformPosition(index, priority)`: walk ops until offset > index (or ==
  when !priority for inserts): deletes reduce index, inserts increase.
- `invert(base)`: for each op of `this`, inserts become deletes; deletes
  become the sliced-out base content; retains with attrs become retains with
  `attributes::invert`, computed per base op in the range.
- `diff(other, hint)`: requires both to be documents (insert-only). Build two
  strings where each embed is `\0`; run fast-diff with the hint; walk the
  diff result consuming ops: EQUAL → compare ops pairwise; equal content →
  `retain(len, attributes::diff(a, b))`, otherwise push `other` insert and
  `delete(len)`; INSERT → push `other` ops; DELETE → `delete(len)`. Then
  `chop()`.
- `eachLine`: iterate inserts only; split text at `newline`; when a newline
  is hit, call `fn(line, newlineOp.attributes, index)`; stop when `fn`
  returns false; call once more at the end if the line is non-empty.
- `length` sums op lengths; `changeLength` is inserts minus deletes.
- `slice(start, end)`: copies ops intersecting the range, cutting partial
  text, using the iterator.
- Attribute algebra, from `AttributeMap.ts`:
  - `compose(a, b, keepNull)`: result = copy of b (dropping nulls unless
    keepNull); for each key in a not present in b, copy a's value. Empty →
    nullopt.
  - `diff(a, b)`: for every key in either, if values differ, result[key] =
    b has key ? b[key] : null. Empty → nullopt.
  - `invert(attr, base)`: keys in base whose attr value differs → base value;
    keys in attr absent from base → null. Always returns a map.
  - `transform(a, b, priority)`: a empty → b; b empty → nullopt; !priority →
    b; priority → keys of b not present in a. Empty → nullopt.
- Embed retains (`retain: {image: {...}}`, Quill 2 embed handlers) are not
  supported in v1: parsing accepts them as opaque retains of length 1 and
  compose/transform treat them like plain retains. flutter_quill does not
  emit them.

### JSON

- Parse: `QJsonArray` (flutter_quill: bare array of ops), `QJsonObject` with
  `"ops"` (Quill.js), or a `QJsonDocument`/`QByteArray` wrapping either.
  Invalid ops produce `Json::Error` with index and message; the parser does
  not silently drop ops.
- Serialize: `toJsonArray()` (default, flutter_quill shape) and
  `toJsonObject()` (`{"ops": [...]}`). `toJson(QJsonDocument::Compact)`
  helper. Attribute key order is sorted, which `QJsonObject` does anyway.
- Numbers: Delta integers (`retain`, `delete`, `header`, `indent`) are read
  as `int`; `QJsonValue::toInt` with validation that the value is integral.
  Attribute values are stored as `QJsonValue` so strings, bools, numbers and
  nested objects (flutter_quill may store objects in custom attributes)
  round-trip unchanged.

### fast-diff port

`diff()` results are not unique; the ported slab tests assert exact deltas,
so the diff algorithm must be the one slab uses: `fast-diff` (common
prefix/suffix trimming, half-match heuristic, Myers bisect, cursor-position
fix-up for the hint). Port ~400 lines from `fast-diff/diff.js`, operating on
`QString` (UTF-16 code units, same as JS, so surrogate handling matches).
Keep the Apache-2.0 notice in the source header and in
`THIRD_PARTY_NOTICES.md`.

## Tasks

- [x] `Attributes.h/.cpp` with the four functions and `tst_attributes`
      ported from `test/attributes/*.ts`.
- [x] `Op`, `OpIterator`, `Delta` builder (`insert/retain/remove/push/chop`)
      with `tst_delta_builder` from `test/delta/builder.ts`.
- [x] `length/changeLength/slice/concat/eachLine` + `tst_delta_helpers`
      from `test/delta/helpers.ts`.
- [x] `compose` + `tst_delta_compose` from `test/delta/compose.ts`.
- [x] `transform`/`transformPosition` + tests from `test/delta/transform.ts`,
      `transformPosition.ts`.
- [x] `FastDiff` port; `diff` + `tst_delta_diff` from `test/delta/diff.ts`.
- [x] `invert` + `tst_delta_invert` from `test/delta/invert.ts`.
- [x] JSON parse/serialize + `tst_json` (both shapes, error cases,
      flutter_quill fixture files in `tests/fixtures/flutter_quill/`).
- [x] `THIRD_PARTY_NOTICES.md`.

## Tests

- Every `it(...)` in slab/delta's test directory becomes one QtTest case,
  same name, same data. Target is a 1:1 port, roughly 250 assertions.
- Property checks (QtTest data-driven with random seeds, fixed seed in CI):
  `a.compose(a.diff(b)) == b`; `base.compose(d).compose(d.invert(base)) == base`;
  `a.compose(a.transform(b, true)) == b.compose(b.transform(a, false))`
  (OT convergence) over random small documents and changes.
- JSON: parsing a flutter_quill export and serialising it back yields
  byte-identical compact JSON for every fixture.

## Exit criteria

- All ported slab tests and property tests pass in Debug and Release.
- No Qt GUI/Widgets dependency in `QuillQt::Delta` (checked with
  `target_link_libraries` review and a link test).

## Risks

- Subtle `push` ordering or `keepNull` mistakes break OT convergence; the
  property tests are there to catch what the ported cases miss.
- fast-diff port diverging on surrogate pairs; include a test with emoji
  (flutter_quill users type emoji constantly).

## Outcome (2026-11-08)

All tasks done. Debug, Release and clang builds have zero warnings; all
suites also pass under AddressSanitizer and UBSan.

| Suite | Cases | Checks |
|---|---|---|
| `tst_slab_recorded` | 662 | every call slab/delta's 180 specs make, replayed |
| `tst_fastdiff` | 1510 | fast-diff 1.3.0 outputs, incl. cursor and emoji cases |
| `tst_op` | 17 | hand port of op and iterator tests |
| `tst_delta_api` | 15 | callback helpers, errors, embed handlers, cursor hint |
| `tst_json` | 20 | both JSON shapes, error cases, flutter_quill example |
| `tst_properties` | 8 × 2000 | diff, invert, TP1 convergence, transformPosition, associativity, JSON, slice |

### Deviations from the plan

- **Test porting strategy.** Instead of retyping ~1500 lines of TypeScript,
  `tools/oracle/record-slab-tests.sh` runs slab/delta's own suite with a
  recorder and `tst_slab_recorded` replays every call (result, receiver
  mutation, thrown message). This covers more than the assertions do,
  including intermediate builder results. Tests whose subject is not a Delta
  call (iterator, `Op.length`, callback helpers) are hand-ported.
- **`AttributeMap` is `QJsonObject`**, not `QMap<QString, QJsonValue>`, and
  the attribute functions return a map instead of `std::optional`. An empty
  map stands for JavaScript `undefined`; the recorded tests confirm no
  observable difference.
- **Embed changes are fully supported**, not treated as opaque: the Quill 2
  embed-handler registry (`Delta::registerEmbed`) is ported, because the
  upstream tests exercise it and it costs ~60 lines.
- **No separate `Json.h`.** JSON lives on `Delta` (`fromJson`, `toJson`,
  `toJsonArray`, `toJsonObject`) and `Op` (`fromJson`, `toJson`).
- **Errors are exceptions** (`quill::DeltaError`) with the upstream
  messages, for misuse slab/delta also throws on.
- **Byte-identical JSON is not achievable**: `QJsonObject` sorts keys, so
  attribute key order inside an op is alphabetical. Serialisation writes the
  operation key first and `attributes` last (Quill.js and flutter_quill
  order), and values round-trip exactly. Exit criterion changed to semantic
  equality, which `tst_json` checks on the flutter_quill example document.
- **`invert` compares object attribute values deeply**; JavaScript uses
  reference inequality there, which only differs for object-valued
  attributes and is a JavaScript artefact.

### Findings worth knowing

- `transformPosition(i, priority)` uses priority in the opposite sense from
  `transform(x, priority)`: `transformPosition(i, true)` keeps the cursor
  before a concurrent insert at `i`. Verified against quill-delta and
  encoded in `tst_properties`. Matters for M5 cursor handling.
- fast-diff's semantic cleanup lets its equality stack index reach -1 and
  reads `equalities[-1]` as a plain property; the port emulates this.
- `Delta.diff()` runs fast-diff with cleanup and surrogate fixing at the top
  level only; recursive calls use neither. The port keeps that distinction.

### Later fix (M5)

The M5 random editing tests found two inputs on which fast-diff 1.3.0
recurses until the JavaScript stack overflows (confirmed with the npm
package): a surrogate pair split in only one of the two texts, e.g.
`"😀"` vs `"\uD83Dx\uDE00"`. The port now returns delete + insert
when a bisect split makes no progress and stops `diffCleanupMerge` after 64
sweeps (each sweep keeps the diff valid). Regression cases are in
`tst_fastdiff::splitSurrogatePairDoesNotRecurseForever`; a 10-minute random
fuzz of short strings with lone surrogates found no further crash.
