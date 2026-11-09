# M11 — Large documents: measure, set budgets, fix the hot paths

Estimate: 2–3 days (1 measuring, the rest depends on what the numbers say).
Depends on: M5 (change tracking), M8 (fuzzers, CI). Comes before M12: the
collaboration example applies every remote keystroke through
`updateContents`, so its cost matters there.

## Goal

Know how quill-qt behaves on big documents, make the common operations fast
enough to feel instant, and stop regressions with a check that does not
depend on the CI machine's speed.

## What reading the code suggests (to be confirmed by measuring)

| Path | Work per operation today | Size it grows with |
|---|---|---|
| Typing (`onContentsChange`) | exports the changed **lines** from the `QTextDocument` and diffs them against `contents.slice`; then `contents = contents.compose(change)` | line length; number of ops (compose rebuilds the whole op list) |
| API edits (`modify` + `apply`) | `DeltaDocument::apply` composes into the whole document; `modify` composes again and compares `old.compose(input) == contents`; `apply` compares `old.compose(change) == target` | number of ops, three or four times per edit |
| Undo / redo (`resync`) | full `toDelta` export plus `old.diff(full)`: fast-diff over the whole document text | document length |
| `textChange(change, oldContents)` | passes the old Delta; `QList` is implicitly shared, so it should be a pointer copy | should be constant; verify |
| Selection change | toolbar refresh calls `getFormat` | selection length |
| Load | op-by-op cursor inserts plus restyle passes and `updateBlockSpacing` over all blocks | document size; probably fine |
| Paint | decorations loop starts at the first visible block and breaks after the viewport | viewport; fine |
| Verify mode (`QUILLQT_VERIFY=1`) | full export per change | document size; test-only, no budget |

A 200 000-character paragraph is the worst case for typing, a document with
many small formatted runs (many ops) for API edits, and any big document
for undo.

## Deliverables

```
tools/bench/                      quill-bench executable (Release only, not installed)
  main.cpp                          --corpus NAME --scenario NAME --json out.json
  Corpora.h/.cpp                    deterministic document generators
tests/bench/tst_scaling.cpp       complexity check: time(2n) / time(n), runs in ctest
ci/bench.sh                       runs quill-bench, prints a table, compares with a baseline
plans/M11-large-documents.md      outcome: numbers before and after
```

## Design

### Corpora (generated, seeded, not stored)

| Name | Shape |
|---|---|
| `mixed-10k` | 10 000 lines (~1 MB): headers, paragraphs with bold/italic/link/colour runs, lists with indents, quotes, code blocks, an image every 200 lines (data URLs ~2 KB) |
| `plain-100k` | 100 000 short plain lines (~5 MB) |
| `long-line` | one paragraph of 200 000 characters with a format run every 50 characters |
| `many-embeds` | 2 000 images and videos alone on their lines, local files |
| `nested-lists` | 5 000 list items, indents 0–8, three list types mixed |
| `flutter-sample-x200` | flutter_quill's own sample repeated 200 times (realistic attribute mix) |

### Scenarios

Load (`setContents`), `getContents`, export `toDelta`, first paint; typing 200
characters at the start, middle and end (one `QTest::keyClick` each, layout
and a viewport repaint included, latency per key recorded); Enter and
Backspace that splits and merges lines; `formatLine` on one line; bold on the
whole document; paste 1 000 lines of HTML; undo and redo of that paste; an
API `updateContents` of one character at the middle (the M12 case); find all
for a common word; scroll from top to bottom (paint time per frame); RSS
after load.

Reported: median, p95 and max per scenario, as JSON and a table.

### Budgets (Release build, this machine; checked in `ci/bench.sh` with 3× slack)

| Scenario | Budget |
|---|---|
| Load `mixed-10k` | ≤ 500 ms |
| Keystroke on `mixed-10k`, `nested-lists` | ≤ 8 ms median, ≤ 16 ms p95 |
| Keystroke on `long-line` | ≤ 16 ms p95 |
| One-character `updateContents` on `mixed-10k` | ≤ 4 ms median |
| Undo / redo of a 1 000-line paste | ≤ 150 ms |
| `getContents()` | constant (shared copy) |
| Scroll frame | ≤ 16 ms |

Numbers are adjusted after the first measurement if one is unrealistic for
Qt itself (e.g. Qt's own layout of a 200 000-character line); then the
budget becomes "Qt's time + 20 %" and is written down.

### Likely fixes (each only if the numbers ask for it)

- **Compose and slice that do not rebuild the document**: a change that is
  a long retain, an edit and a long retain only touches a few ops. Add
  `Delta::composeInPlace` (or make `compose` splice) using a running
  offset index, so typing costs O(ops near the edit) instead of O(all ops).
  Results must equal `compose`'s exactly: property tests and the oracle
  run against both.
- **Drop redundant whole-document comparisons**: `modify` and `apply`
  check `old.compose(x) == contents` to decide which Delta to report. Where
  the answer is known by construction, skip it; where it is a real guard,
  keep it but run it only in verify mode, and let the fuzzers (which run
  with verify on) keep proving the shortcut.
- **Undo/redo without a full diff**: collect the union of
  `contentsChange` ranges during the replay, export and diff only those
  blocks (the same windowing `onContentsChange` uses for typing), and
  fall back to the full resync when the counts are inconsistent. The
  M5 note on Qt's unreliable ranges for grouped commands is why it is
  full today; the union plus the fallback keeps that safe.
- **Long lines**: export and diff only the changed part of a line plus its
  newline (inline attributes need the line's block attributes, which the
  newline op carries).
- **Toolbar refresh**: coalesce to one per event-loop turn.

### Regression guard that works on any machine

`tst_scaling` (in ctest, Release and Debug, a few seconds): for typing, a
one-character `updateContents`, undo, and load, time the operation on a
corpus of size n and 4n; fail if the ratio is above 2 for operations that
should be constant in n (typing, small updates) or above 8 for linear ones
(load). Ratios are robust against slow CI runners; absolute budgets stay in
`ci/bench.sh`, which CI runs and reports but does not fail on.

## Tasks

- [ ] `tools/bench` with corpora and scenarios; first full measurement on
      Qt 6.4.2 and 6.8.3; numbers in the outcome section.
- [ ] Profile the scenarios over budget (`perf record` is available) and
      list where the time goes.
- [ ] Fixes as the numbers require (list above), each with before/after
      numbers.
- [ ] Exactness: `tst_properties` and the oracle on any new Delta function;
      the long fuzz runs (3000 API steps, 60 000 keys) with verify on, on
      Qt 6.4.2 / 6.8.3 / 6.12.
- [ ] `tst_scaling` in ctest; `ci/bench.sh` in a CI job that uploads the
      JSON.
- [ ] README: "Performance" paragraph with the numbers and limits (e.g.
      "verify mode is O(document) per change").

## Exit criteria

- Every budget met, or the gap explained as Qt's own cost and recorded.
- `tst_scaling` green; full suite and long fuzzers green on all M9a
  configurations; oracle unchanged.

## Risks

- **Qt's own costs** (`QTextDocument` layout of long paragraphs, image
  layout) may dominate; those are outside quill-qt's control. Measure Qt
  alone (a plain `QTextEdit` with the same document) as the baseline.
- **Fast paths change exactness**: every shortcut in change tracking risks a
  Delta that differs from the document. Verify mode and the fuzzers exist
  for this; run them on every fix.
- Timing tests are flaky on shared runners; only ratio checks fail the
  build.
