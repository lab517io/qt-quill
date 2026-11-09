# M10 — Rendering fidelity: spacing, code panel, formula scripts

Estimate: 1–1.5 days. Depends on: M7 (embeds), M8 (spacing, flutter_quill
check). Independent of M9.

## Goal

Close the visible layout differences to flutter_quill that M5–M8 recorded
but left open, and replace the hand-copied spacing values with values
**measured in flutter_quill**, so the remaining differences are known
numbers instead of impressions:

1. Vertical spacing around images and videos on their own line.
2. Group spacing by identical block attributes, as flutter_quill groups
   lines (an indent change starts a new group), instead of by kind.
3. Vertical padding inside the code-block panel.
4. Formulas with `script: super/sub` drawn raised or lowered.

None of these changes the Delta: they are layout-only properties that the
exporter ignores (like `prop::GroupSpacing` today), so every round-trip and
parity fixture must stay byte-identical.

## Open items this closes (from earlier plans)

| Item | Recorded in | Today |
|---|---|---|
| No spacing around block embeds | M8 "Notes from M7" | image/video lines get no margins |
| Group spacing by kind | M8 Deviations | `spacingGroup()` returns list / quote / code; indent and list-type changes do not start a group |
| Code panel without vertical padding | M5 Deviations | `codeBlockPadding` is applied left/right only; the panel touches the first and last line |
| Formula `script` not drawn | M8 "Notes from M7" | the attribute round-trips; the object stays `AlignMiddle`-centred |

## Design

### Measure first: a geometry probe in `tools/flutter-check`

- New `test/geometry_test.dart`: loads each fixture into a `QuillEditor`
  widget, pumps it at a fixed width (800 px) and walks the render tree for
  the line boxes (`RenderEditableTextLine` / embed proxies). For every line
  it records `{index, kind, top, height}` in logical px, and writes
  `tests/fixtures/flutter_quill/geometry/<fixture>.json` (with
  `--update`; otherwise it checks the files are current, like the oracle
  drift check).
- Absolute heights depend on fonts (flutter tests use the square test font),
  so the comparison is on **gaps**: `gap(n) = top(n+1) - (top(n) +
  height(n))`, i.e. the spacing between line boxes, which comes from
  `VerticalSpacing` and paddings only.
- `tst_flutter_parity::geometry` loads the same fixtures in quill-qt at the
  same width and compares gaps per line, tolerance 1 px × scale. Known
  differences go in `known_differences.json` with a reason, as the
  round-trip ones do.
- Fixtures: the existing flutter_quill samples, plus a new
  `spacing_matrix.json` that has every block kind next to every other
  (header levels, lists with indent and type changes, quote, code with and
  without language, image and video alone on a line, an image inline with
  text, a formula with each script).
- Block embeds in flutter_quill come from `flutter_quill_extensions`'
  builders, which flutter-check does not depend on yet; add it as a dev
  dependency pinned to the version that matches 11.6.0. If it does not
  resolve, take the paddings from its source and record that the values are
  copied, not measured.

### Spacing around block embeds

- A line whose only content is one block embed (`EmbedRenderer::isBlock()`
  and alone on its line, the same test `insertBlockEmbed` and the video
  sizing use) gets the measured top/bottom margin, stored in a new
  `prop::EmbedSpacing` the way `prop::GroupSpacing` is stored, so the
  exporter and the "pure format mapping" stay untouched.
- Kept up to date by the same pass as group spacing,
  `DeltaDocument::updateBlockSpacing`. Typing text next to the image turns it
  inline, which removes the spacing; deleting the text adds it back. Check
  every edit path that changes a line's content calls it (typing,
  `apply()`, paste, undo resync), not only restyles.
- `StyleOptions::blockEmbedSpacingTop/Bottom` with the measured defaults,
  scaled like the other spacings.

### Group spacing by block attributes

- `spacingGroup(b)` becomes a key of the line's block attributes as
  flutter_quill groups them (`list` value, `indent`, `blockquote`,
  `code-block`, and whatever the probe shows matters; alignment and
  direction probably do not start a group). Two neighbouring lines are in
  the same group when their keys are equal.
- The quote bar and code panel painting keep merging by kind unless the probe
  shows flutter_quill draws separate panels (likely for code blocks with a
  different language); then the panel run breaks on the same key.

### Code panel padding

- First and last line of a code run get top/bottom margins of the measured
  padding (default `codeBlockPadding`), again as a layout-only property, and
  the panel rect includes them; it already uses `blockBoundingRect` plus
  margins since M8.
- The caret and selection must not show in the padding; check clicks in
  the padding land on the first/last code line.

### Formula `script`

- Qt's layout places inline objects by their size and `AlignMiddle` /
  `AlignBaseline` / `AlignTop` / `AlignBottom`, but ignores
  `AlignSuperScript` / `AlignSubScript` for objects. Draw the shift inside
  the object instead: for `script`, the object's height grows by the
  offset Qt uses for text (about a third of the ascent), and `paint()`
  draws the formula in the upper or lower part. `size()` and `paint()`
  receive the attributes already (`EmbedContext`).
- Inline image embeds with `script` get the same treatment.

## Tasks

- [x] Geometry probe in flutter-check (`geometry_test.dart`, `WRITE_FIXTURES`
      mode, drift check through `ci/check-flutter.sh`); `spacing_matrix.json`.
- [x] `flutter_quill_extensions`: read instead of depended on (see Findings;
      its builders need network images and the video_player plugin).
- [x] `tst_flutter_parity::geometry` comparing gaps; differences found and
      fixed listed below.
- [x] Block embed spacing: none in flutter_quill; the image line height
      (image + text descent) fixed instead.
- [x] Spacing by block attributes, flutter_quill's way; code panels split
      per block.
- [x] Code panel vertical padding (16) and horizontal padding (4).
- [x] Formula `script`: not drawn raised, as in flutter_quill (see Findings).
- [x] Exports unchanged, screenshots refreshed, docs.

## Tests

| Test | What it proves |
|---|---|
| `geometry_test.dart` (flutter) | probe files are current for flutter_quill 11.6.0 |
| `tst_flutter_parity::geometry` | quill-qt gaps match flutter_quill within 1 px, or a listed reason |
| `tst_load` / `tst_editor_changes` | spacing follows edits: image turns inline and back, indent change splits a group, code lines added and removed |
| `tst_embeds` | formula with `super`/`sub` painted above/below the centre (grab and check the ink rows); size grows by the offset |
| existing round-trip, export, parity | the Delta does not change |

## Exit criteria

- Geometry gaps match flutter_quill on all fixtures, or each difference is
  listed with a reason.
- All exports byte-identical to before; full suite green on the M9a
  configurations (Debug, ASan, clang, Qt 6.8.3 / 6.12, Mingw).

## Risks

- **Render-tree walking is version-specific**: the render box names are
  flutter_quill internals. Keep the probe small and pinned to 11.6.0; if a
  type is not reachable, measure through `getBoxesForSelection` of each
  line instead.
- **Spacing that depends on content** (embed alone on its line) is new: the
  property must be refreshed on every content change, or layout drifts
  silently. The edit fuzzers get a check that recomputing spacing from
  scratch changes nothing.
- flutter_quill padding may be in `EdgeInsets` around whole blocks rather
  than per line; the property mechanism handles both, as long as the probe
  says which.

## Outcome (2026-11-09)

Every gap between lines in the ten probed documents matches flutter_quill
11.6.0 within 1 px (line pitch within 4 % after lines with a line height);
`known_differences.json` is empty. No exported Delta changed.

### Findings about flutter_quill (from its source, confirmed by the probe)

- **Spacing is per block, not per line.** Consecutive lines with equal block
  attributes other than `header` (`list`, `indent`, `code-block`,
  `blockquote`, `align`, `direction`, `line-height`) form a block. A block
  has its own spacing (6 above for list, indent, quote and code blocks,
  none for alignment, direction or line height); each line in it gets the
  block style's line spacing, except no top spacing on the first line and
  no bottom spacing on the last. Headers use their spacing as line spacing.
- **Indented paragraphs** are blocks too: 6 above, 6 between lines.
- **Code blocks** have content padding 16 above and below, 4 left and
  right, inside the background; a different `code-block` value (a
  language) starts a new block with its own panel.
- **flutter_quill adds spacing; Qt collapses it.** Two adjacent margins in
  `QTextDocument` give the larger one, as in CSS; flutter_quill adds them
  (quote lines: 2 below + 6 above = 8).
- **No spacing around images or videos.** The M8 note was wrong:
  `flutter_quill_extensions` 11.0.0 parses its `margin` style property with
  `double.tryParse('margin')` (the literal string), so it is never applied,
  and the video builder's margin defaults to 0. An image alone on its line
  sits on the text baseline: the line is the image plus the font descent.
- **`line-height`** knows the toolbar's four values: 1, 1.15, 1.5 and 2 are
  drawn as text heights 1.15, 1.30, 1.55 and 2.0 times the font size; other
  values have no effect.
- **`script` on embeds** has no effect (flutter_quill's sub/superscript are
  font features of text spans; a formula embed is a widget span). Quill.js
  raises them (`<sup>`); quill-qt follows flutter_quill and keeps formulas
  centred on the text.

### Changes

- `formats::lineSpacing()` / `formats::blockKey()`: flutter_quill's rules
  above. `DeltaDocument::updateBlockSpacing()` puts the whole gap between
  two lines into the lower line's top margin (nothing for Qt to collapse);
  `prop::DecorationTop` / `prop::DecorationBottom` (replacing
  `prop::GroupSpacing`) say how far the quote bar and code panel reach into
  the margins. `formats::blockFormat()` no longer sets vertical margins.
- `StyleOptions`: `codeBlockPaddingVertical` (16), `indentLineSpacingBottom`
  (6); `codeBlockPadding` 8 → 4.
- Images alone on their line reserve the font descent below them (in the
  embed object's size, so it follows every edit and relayout).
- `line-height` maps flutter_quill's four values to Qt percentages of the
  natural line height (which corresponds to flutter_quill's paragraph
  height 1.15): 100 / 113 / 135 / 174; other values are preserved without
  effect. Export maps back; a percentage set through Qt's API still exports
  as percent / 100.
- Ctrl+Backspace / Ctrl+Delete (and the other word and line deletion keys)
  go through the editor's deletion path like Backspace and Delete: Qt's own
  word deletion joined lines without flutter_quill's merge rule and could
  not update the spacing in the same undo step. Other Qt-internal edits
  that leave spacing stale are fixed up after the change.
- Verify mode (`QUILLQT_VERIFY`, the fuzzers) also checks that every line's
  margins are current.

### Tests

- `tst_flutter_parity::geometry` (10 documents), `tst_editor_changes::
  spacingFollowsEdits` (list spacing, Ctrl+Backspace join and one-step undo,
  code padding), `decorationsArePainted` (padding inside the panel),
  `tst_embeds::sizes` (descent below a lone image), `tst_load` line heights.
- Long fuzz runs with verify on (3000 API steps, 60 000 keys): clean.
- flutter check: 80 tests (+10 geometry, +2 for `spacing_matrix.json`).
- All M9a configurations pass 34/34 (Debug, Release, ASan, clang with
  `-Werror`, Qt 6.8.3, Qt 6.12.0, Windows under Wine). Under Wine, Qt's
  offscreen platform has no fonts, so there is no text descent below a lone
  image; the two checks that depend on it run only when fonts exist.

### Deviations

- No `flutter_quill_extensions` dependency: the probe uses a box builder
  that does what its image builder does, because the real builders need
  network images and a video plugin in tests. Videos are not measured
  (their size depends on the video).
- Formula `script` is not drawn raised (flutter_quill does not either).
- Qt's line height puts the extra space below the text, flutter_quill
  spreads it around; the line pitch matches, the text sits a little higher.
