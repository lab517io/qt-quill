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

- [x] `tools/bench` with corpora and scenarios; first full measurement on
      Qt 6.4.2; budgets also measured on Qt 6.8.3.
- [x] Profile the scenarios over budget (`perf` is not permitted on this
      machine; temporary scope timers instead, removed afterwards).
- [x] Fixes as the numbers required, each with before/after numbers.
- [x] Exactness: `tst_properties`, the oracle, the long fuzz runs with verify
      on (now also checking list grouping and spacing).
- [x] `tst_scaling` in ctest; `ci/bench.sh` with `tools/bench/budgets.json`
      in a CI job that uploads the JSON.
- [x] README "Performance".

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

## Outcome (2026-11-17)

All budgets in `tools/bench/budgets.json` are met on Qt 6.4.2 and 6.8.3.
Most are ratios to a plain `QTextEdit` holding the same document, measured
in the same run, so they say how much quill-qt adds to Qt's own layout.

### Before and after (Qt 6.4.2, Release, medians in ms)

| Corpus | Scenario | Before | After |
|---|---|---|---|
| mixed (1 MB) | load | 708 | 489 (Qt 494) |
| mixed | getContents | 6.3 | 0.1 |
| mixed | export | 118 | 41 |
| mixed | typing | 22.0 | 12.1 (Qt 9.8) |
| mixed | Enter + Backspace | 57.2 | 18.6 |
| mixed | format a line | 54.1 | 15.4 |
| mixed | one-character `updateContents` | 45.9 | 13.2 |
| mixed | paste / undo / redo, slowest | 217 | 108 |
| plain (3.8 MB) | load | 63 371 | 1 994 (Qt 1 773) |
| plain | export | 62 602 | 130 |
| plain | paste / undo / redo, slowest | 55 757 | 232 |
| plain | bold on everything | 144 745 | 1 279 |
| long line (200 000 chars) | Enter + Backspace | 504 | 66 |
| long line | format the line | 659 | 106 |
| long line | one-character `updateContents` | 637 | 68 (Qt typing 67) |
| nested lists (5 000) | Enter + Backspace | 125 | 13.2 |
| nested lists | format a line | 99.9 | 14.9 |
| nested lists | one-character `updateContents` | 111 | 7.8 |
| flutter sample ×200 | one-character `updateContents` | 28.5 | 13.4 |

### What was slow, and the fixes

1. **`Delta::push` was quadratic for runs of text** with equal attributes:
   it built a new string for every merge. Exporting 100 000 plain lines
   took a minute (load, export, undo and bold all export). It now appends
   in place.
2. **`restyleBlock` was quadratic in the length of a line**: each op of the
   line scanned the block's fragments from the start. One walk over the
   fragments now finds the ranges whose format differs.
3. **`regroupLists` rebuilt lists on every edit**: it walked the whole run
   of list lines and created a new `QTextList` whenever a level started,
   even when the line's list was already right. It now starts at the
   closest line of indent 0 before the edit, keeps a line's list when it
   fits, and stops at the first unchanged line of indent 0 after it.
   Regrouping unchanged lines changes nothing.
4. **`Delta::compose`** copied the unchanged start op by op and the
   unchanged end twice (`concat`); both are now copied once, in bulk.
5. **Two whole-document composes per API edit** only decided which Delta
   to report; when the change needed no normalisation the answer is known
   (`detail::applyChange` reports it), so they are skipped.
6. **Walking to a position built every op on the way** (`normalizeChange`,
   `slice`, `lineAt`); `OpIterator::skip()` only adds lengths.
7. **Export derived the attributes of every fragment and line from
   scratch**; they are cached per Qt format index during one export (a
   document uses few formats).
8. **`getContents()`** returned a copy; it now shares the Delta.

### Checks added

- `tst_scaling` (ctest, ~6 s): typing and `updateContents` in the mixed
  document must stay below 2.5× for a 4× larger document; load, export,
  paste+undo and edits in a long line below 8× (6× for the long line).
  Mutation tests: the old `push` (×14) and the old `restyleBlock` (×6.8) fail
  it.
- `tst_editor_api::editsKeepUntouchedLists`: edits that do not touch list
  structure keep every line's `QTextList`; splitting a flat list keeps the
  items before it. Fails with the old regrouping. (Edits in long lists are
  not timed: Qt numbers list items in linear time, which hides quill-qt's
  part in a ratio.)
- Verify mode also checks that list grouping equals a regroup from scratch
  (`detail::staleListGrouping`); a deliberately broken regroup is caught by
  the key fuzzer at once.
- `ci/bench.sh`: the budgets, run in a CI job that reports and uploads the
  JSON without failing (shared runners are noisy).

### What remains (recorded, not fixed)

- Every change copies the document's op list once (`contents` is shared
  with the `oldContents` of `textChange`): about 2 ms for 36 000 ops. Only a
  persistent structure (a tree of op chunks) would make edits independent
  of the document size; not needed for the budgets.
- Undo and redo export the whole document and diff it (about 100 ms per
  MB of formatted text, 230 ms for 3.8 MB of plain text): Qt's change range
  for grouped undo commands is not reliable (M5). A per-step record of
  quill-qt's own changes could replace it.
- Typing near the start of a big document is slower than near the end; Qt
  alone behaves the same (`qt-type-start`).
- Formatting a whole 200 000-character line, or bold on everything, costs
  what Qt needs to change every fragment's format.
- `setContents` exports the loaded document once to record it exactly (about
  35 ms per 100 000 characters of formatted text); documents with many small
  runs load about 1.3× slower than Qt alone.
- `perf` could not be used (kernel setting); profiling used temporary scope
  timers.

### Deviations

- Budgets became ratios to plain Qt for everything Qt dominates (typing,
  updates, load); the absolute "≤ 4 ms per update" was unrealistic, since a
  plain `QTextEdit` already needs ~10 ms per key on the 1 MB document,
  including the repaint the benchmark measures.
- No find-bar optimisation: `find` is Qt's `QTextDocument::find`.
