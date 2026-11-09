# M8 — Fidelity harness and CI

Estimate: 2 days initial, then ongoing. Depends on: M2 (oracle for OT),
M4 (oracle for documents). Can start as soon as M2 is done.

## Goal

Prove compatibility mechanically: the C++ Delta library agrees with the
reference `quill-delta` npm package on compose/transform/diff/invert, the
converters preserve real flutter_quill documents, and every commit is built
and tested on CI.

## Deliverables

```
tools/oracle/
  package.json            quill-delta (pinned), optional: quill + jsdom for getContents()
  generate.mjs            reads tests/fixtures/ot/cases/*.json, writes tests/fixtures/ot/expected/*.json
  html.mjs                optional: Delta -> Quill getSemanticHTML() for visual comparisons
  README.md
tests/fixtures/ot/
  cases/                  hand-written + generated random cases (seeded), committed
  expected/               oracle output, committed (CI passes without Node)
tests/delta/tst_oracle.cpp   data-driven QtTest over cases/expected
tests/fixtures/flutter_quill/   real exports (see below)
ci/check-oracle.sh        regenerate expected/ and `git diff --exit-code`
.github/workflows/ci.yml  (or ci/gitlab-ci.yml; pick when the remote is chosen)
.clang-format, ci/format-check.sh
```

## Design

### OT oracle

Case file format (one JSON per case):

```json
{ "name": "compose insert over retain", "op": "compose",
  "a": [{"insert":"abc"}], "b": [{"retain":1},{"insert":"X"}] }
```

`op` ∈ `compose | transform | transformPosition | diff | invert | slice |
concat | eachLine`. `generate.mjs` evaluates each with `quill-delta` and
writes `expected/<name>.json`. `tst_oracle` loads every case, runs the C++
implementation, compares JSON after canonicalisation (sorted keys, merged
ops). Random cases are produced by `generate.mjs --random 500 --seed 42`
over a small alphabet with embeds and attributes, so edge cases accumulate
without hand-writing them.

### flutter_quill fixtures

- `tests/fixtures/flutter_quill/`: documents exported from the flutter_quill
  example app (`Document.toDelta().toJson()`) for several versions (9.x,
  10.x, 11.x), covering every toolbar feature, embeds and `custom`. Until a
  Flutter machine is available, fixtures are assembled from the
  flutter_quill repository's own sample assets (`example/assets/sample_data*.json`)
  and the Dart tests' expected outputs. Provenance recorded in a
  `SOURCES.md` next to them.
- Each fixture is checked for: parse without error, round-trip equality
  (M4), `Delta::compose` identity (`doc.compose(Delta()) == doc`).
- Optional Dart oracle (`tools/dart-oracle/`): a small Dart CLI using
  `dart_quill_delta` to compose/transform the same cases, run only when
  `dart` is on PATH. Adds confidence that flutter_quill's Dart port agrees
  with the JS one on the cases we rely on.

### Visual comparison (optional, best effort)

`html.mjs` renders each document fixture with Quill's
`getSemanticHTML()`; a Playwright screenshot of that HTML and the demo's
`--screenshot` PNG are placed side by side in the CI artifacts for human
review. No pixel assertions; typography differs by design.

### CI pipeline (GitHub Actions assumed; adjust if the repo lands elsewhere)

Jobs:

1. `build-test-linux` (ubuntu-24.04): `ci/install-deps-ubuntu.sh`,
   `ci/build.sh` (Debug with `-fsanitize=address,undefined`, and Release),
   `ci/test.sh` offscreen, upload test logs and fixture renders.
2. `oracle` (ubuntu-24.04, Node 20): `npm ci` in `tools/oracle`,
   `ci/check-oracle.sh` fails if committed expected outputs drift from the
   pinned `quill-delta`.
3. `screenshots`: `xvfb-run ci/screenshot.sh`, upload PNGs.
4. `format`: `clang-format --dry-run -Werror` over `src apps tests`.
5. (M9) `build-windows`, `build-macos` via `jurplel/install-qt-action`.

The repository is not under version control yet; M1 creates the repo. The
remote (GitHub/GitLab) decides the workflow file syntax; the scripts under
`ci/` are provider-neutral so only the thin YAML changes.

## Tasks

- [x] `tools/oracle` with pinned `quill-delta`, `generate.mjs`, random
      generator, README.
- [x] Hand-written cases: every example from the slab/delta README and the
      Quill "Designing the Delta format" guide; plus known tricky ones
      (insert vs delete ordering, keepNull in retain over retain,
      transform priority with attributes, diff with embeds and emoji).
- [x] `tst_oracle` data-driven test.
- [x] flutter_quill fixtures + `SOURCES.md`; fixture checks wired into
      `tst_roundtrip`.
- [x] `ci/check-oracle.sh`, `ci/format-check.sh`, workflow YAML.
- [x] ASan/UBSan Debug build in CI.
- [x] Optional: Dart oracle; `html.mjs` + screenshots job.

## Exit criteria

- `tst_oracle` passes on all hand-written and 500 random cases.
- CI green on a clean clone; oracle drift check green.
- All flutter_quill fixtures round-trip.

## Risks

- `quill-delta` releases could change `diff` output (fast-diff version);
  pinning plus the drift job make this visible instead of silent.
- Random generator must produce valid documents for `diff` (insert-only,
  trailing newline); validate in the generator.

## Notes from M6

- `ci/screenshot.sh` already renders every demo sample (light and dark) and
  ctest runs `quill-demo --screenshot` as a smoke test; the CI job can
  upload `build/*/screenshots/` as artefacts.
- Visible difference to flutter_quill: no vertical spacing around headings
  and paragraphs (flutter_quill's DefaultStyles have per-header
  `VerticalSpacing`). Map it to block top/bottom margins in
  `formats::blockFormat` (pure, so export stays exact).

## Notes from M7

- Embed rendering goes through `EmbedRegistry` (`src/widgets/Embeds.cpp`);
  screenshot comparisons of `embeds_*.json` need remote images off or a
  local server (`tst_embeds` has a `QTcpServer` stub).
- Block embeds have no vertical spacing around them yet (flutter_quill pads
  image and video blocks); handle with the heading/paragraph spacing.
- Formulas with `script` keep the attribute but are not drawn raised or
  lowered.

## Outcome (2026-11-09)

Done. Debug, Release and ASan/UBSan clean; 18 ctest entries green
(`tst_oracle`, `tst_flutter_parity` added); the flutter_quill check passes
(23 round trips, 21 renders + 2 documented skips, 3 generated documents and
21 scripts unchanged). Flutter 3.47.9 became available on this machine, so
the flutter_quill side is automated instead of manual.

- **OT oracle**: `tools/oracle/generate.mjs` evaluates
  `tests/fixtures/ot/cases/` with quill-delta 5.1.0: 74 hand-written cases
  and 500 seeded random scenarios × 8 operations (compose doc/changes,
  transform, transformPosition, invert, diff, eachLine, slice; random
  documents with embeds, emoji kept whole, attributes with nulls). All 4074
  match in C++ (`tst_oracle`), first run. Output is one case per line.
- **Dart oracle** (`tools/dart-oracle`, dart_quill_delta 10.8.3): agrees on
  everything it supports (3468 identical, 4 equal when canonical, 91 inverts
  with the same effect). One divergence, listed with its reason: Dart's diff
  splits an emoji surrogate pair.
- **flutter_quill check** (`tools/flutter-check`, flutter_quill 11.6.0,
  headless `flutter test`): all quill-qt fixtures and quill-qt editor exports
  (`tests/fixtures/quillqt_exports/`, written by `tst_flutter_parity`) load
  without loss and render; flutter_quill-built documents
  (`tests/fixtures/flutter_quill/generated/`) round-trip in quill-qt
  (`tst_export`); 21 editing scripts recorded in a real flutter_quill editor
  replay identically in `QuillEditor`. This closes the M6 exit criterion.
- **Keyboard parity fixes** found by the scripts: flutter_quill rules by
  default (`QuillEditor::EditingRules`, Quill.js rules selectable); bugs
  fixed in both modes: deleting a multi-line selection kept the first line's
  format (Qt), typing after a link set by `setSelection` extended it, typing
  after leaving a quote / code block kept its colour / font, Tab inside list
  item text indented instead of inserting "\t".
- **Fidelity**: flutter_quill's vertical spacing (headings 16/8/8/6/6/4 px
  above, list lines 6 below, quote lines 6/2, 6 above each list / quote / code
  group), scaled to the base font, in `StyleOptions`; group spacing by
  `DeltaDocument::updateBlockSpacing`; quote bar and code panel exclude it.
- **CI**: `.github/workflows/ci.yml` (build/test Debug, Release, Asan;
  oracle drift; flutter_quill; screenshots artefact; clang-format-18), all
  steps via `ci/*.sh`. `ci/build.sh Asan`, `ci/check-oracle.sh`,
  `ci/check-flutter.sh`, `ci/format-check.sh [--fix]`; `.clang-format` added
  and the tree formatted once.

### Findings about flutter_quill (recorded, not copied)

- `Document.fromJson` moves an inline video onto its own line, and adds a
  "\n" between two consecutive videos (its own example sample changes on
  load).
- Rendering throws for invalid attribute values (`"header": 7`) and for
  `custom` embeds whose value is an object, not JSON, or has non-string
  inner data (`CustomBlockEmbed` is a `BlockEmbed` with String data);
  `CustomEmbedRenderer::encode` documents it and the demo always writes
  string data.
- Lone surrogates (an API index inside an emoji) cannot be laid out.

### Deviations

- Only flutter_quill 11.6.0 is recorded; 9.x / 10.x do not resolve with
  Flutter 3.47.
- No Quill.js HTML / Playwright side-by-side screenshots (optional): the
  flutter_quill render check covers the target editor instead.
- Group spacing is approximate: flutter_quill groups by identical block
  attributes (so e.g. a change of indent starts a new group); quill-qt
  groups by kind (list, quote, code).
- Shift+Tab always outdents (flutter_quill inserts "\t" when the caret is
  inside a list item's text); kept as a UX choice, not a data difference.
