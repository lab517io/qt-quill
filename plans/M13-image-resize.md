# M13 — Image resize handles

Estimate: 1.5–2 days. Depends on: M7 (embeds, image size attributes),
M10 (image line layout, geometry probe).

## Goal

Let users resize images with the mouse, writing the size the way
flutter_quill (with `flutter_quill_extensions`) writes it, so the result
looks the same when the document is opened there. One drag is one change
Delta and one undo step.

## What exists

- Sizes are read from both spellings: the `style` attribute
  (`"width: 200; height: 120"`, flutter_quill_extensions) wins over
  `width` / `height` (Quill.js). `ImageSize.h` already has `parseCss`,
  `removeCssProperties` and `formatLength` (flutter_quill's number format).
- Double click on an image emits `embedActivated`; there is no single-click
  image selection yet.
- `EmbedRenderer::size()` / `paint()` get the attributes through
  `EmbedContext`; `embedAt()` maps viewport points to embed indices.

## Design

### What gets written

- Check `flutter_quill_extensions`' resize code (11.0.0; M10 read its
  image builder, not added as a dependency) for the exact form: which properties, units, number
  format, and whether width only or width and height. Expected:
  `style: "width: 240; height: 135"`, other `style` properties kept
  verbatim.
- If the image's attributes already use Quill.js `width` / `height` (and no
  `style`), update those instead, so a Quill.js document stays in its own
  spelling. `ResizeAttributes::FlutterQuill | QuillJs | Both` on the editor,
  default following the document as above.
- The change is `retain(index) + retain(1, {style: ...})` through
  `formatText(index, 1, attrs, Source::User)`: one undo step, a normal
  `textChange`, and in M12 an ordinary op that transforms like any format
  change.

### Interaction

- **Single click** on an image selects it (selection of length 1 over the
  embed, as Quill does) and shows a frame with four corner handles and a
  size label; Escape, typing or clicking elsewhere clears it. Double click
  still emits `embedActivated`.
- **Dragging a corner** keeps the aspect ratio; Shift resizes freely. The
  preview is painted as an overlay during the drag (no document changes, so
  no relayout per mouse move and no stream of change Deltas); release
  commits once. Escape during the drag cancels.
- Limits: at least 16 px, at most the available text width; sizes rounded
  to whole pixels.
- Handles follow alignment (centred / right-aligned image lines) and RTL;
  hit areas grow at HiDPI so they stay usable; cursors are the diagonal
  resize shapes.
- **Context menu** on a selected image: "Original size" (removes the size
  properties, keeping other `style` content), "Fit width", 25 / 50 / 100 %.
- **Keyboard**: none in this milestone (Quill has none either); noted.
- Read-only editors show no handles.

### API

- `QuillEditor::setImageResizeEnabled(bool)` (default on).
- `EmbedRenderer::resizable() const` (default false; `ImageEmbed` true), so
  video and custom embeds can opt in later. Video is not resizable in M13
  unless flutter_quill_extensions resizes videos too.
- Signals: nothing new; the change arrives as `textChange`.

## Tasks

- [x] Confirm the attribute form from `flutter_quill_extensions`; write it
      down in the plan with the source location.
- [x] Single-click image selection, frame and handles painted in
      `paintEvent`; hit testing; cursors.
- [x] Drag with overlay preview, aspect ratio, Shift, limits, Escape;
      commit via `formatText`.
- [x] Spelling rules (`style` vs `width`/`height`), other `style`
      properties kept.
- [x] Context menu actions.
- [x] `EmbedRenderer::resizable()`, `setImageResizeEnabled`, read-only.
- [x] flutter-check: export a resized image from quill-qt, load it in
      flutter_quill, check the rendered size (with the M10 geometry probe).
- [x] Demo: nothing to add beyond the feature; README "Embeds" paragraph.

## Tests

| Test | What it proves |
|---|---|
| `tst_embeds::resizeDrag` | QTest mouse drag on a corner → one `textChange` with the expected `style`, aspect kept, one undo step restores |
| `tst_embeds::resizeShiftAndLimits` | free resize; clamped to 16 px and the text width |
| `tst_embeds::resizeSpellings` | flutter_quill `style` kept with its other properties; Quill.js `width`/`height` updated in place |
| `tst_embeds::resizeAlignedAndRtl` | handles in the right place for centred, right-aligned and RTL lines; HiDPI variant |
| `tst_embeds::resizeContextMenu` | original size / fit width / percentages |
| `tst_embeds::noHandlesReadOnly` | read-only and disabled editors |
| flutter check | flutter_quill renders the exported size |

## Exit criteria

- Resize by mouse works on Linux at 1× and 2×, all styles; the result
  renders at the same size in flutter_quill.
- One drag = one change Delta = one undo step; suite green on all M9a
  configurations.

## Risks

- **Qt has no native object selection**: the selected state is quill-qt's
  own painting on top of a length-1 selection; Qt's selection highlight
  over the image must be hidden or it looks doubled.
- **Relayout on commit** can move the image (a smaller image moves the
  following text up); the frame must be recomputed from the new layout, not
  the drag rect.
- flutter_quill_extensions may write `mobileWidth`-style attributes in some
  versions; the pinned version decides, and other spellings stay readable.

## Outcome (2026-11-17)

### What flutter_quill_extensions writes

flutter_quill_extensions 11.0.0 resizes through a dialog with width and
height sliders (`lib/src/editor/image/image_menu.dart:60-94`,
`widgets/image_resizer.dart`), which calls
`controller.formatText(offset, 1, StyleAttribute(replaceStyleStringWithSize(...)))`.
`replaceStyleStringWithSize` (`lib/src/common/utils/string.dart`) parses
the existing `style` into ordered `key: value` pairs (pairs without `:`
dropped, keys and values trimmed, a repeated key keeps its first place),
sets `width` and `height` with Dart's `double.toString()` (so whole pixels
are `240.0`) and writes every pair as `key: value; `, with a trailing space.
quill-qt writes exactly that (`detail::replaceStyleStringWithSize`), always
width and height, rounded to whole pixels. Videos have no resize there, so
none here. No `mobileWidth` style attributes are written by this version.

### Done

- `EmbedRenderer::resizable()` (ImageEmbed: true),
  `EmbedRegistry::drawnSize()`.
- `QuillEditor::setImageResizeEnabled()`, `setImageSizeAttributes(Auto |
  FlutterQuill | QuillJs)`, `resizeEmbed(index, size)` (an invalid size:
  natural size).
- Click selects (length-1 selection); frame with four handles and a size
  label; Qt's tint of the selected object and the caret beside it are
  hidden (the image is drawn again over the tint). Corner drag with an
  overlay preview, aspect kept (the axis that changed more), Shift free,
  16 px to the line width, past the opposite corner gives the minimum;
  Escape cancels the drag, or deselects. Release commits one `formatText`
  (one textChange, one undo step in both undo models). Diagonal resize
  cursors over the handles; hit areas larger than the drawn handles.
- Context menu "Image size": original size (size declarations and
  width/height removed, other declarations kept), fit width, 25 / 50 /
  100 % of the natural size.
- Spelling: Auto keeps an image's Quill.js `width` / `height` (string or
  number, as it was) when its style has no size; otherwise the style.
  Forcing Quill.js removes the size from the style, which would win.

### Tests

| Test | Result |
|---|---|
| `tst_image_resize` | style string (flutter_quill_extensions cases), spelling rules, click select (tint hidden, frame on all four edges, no caret, Escape / click elsewhere, double click still activates), drag (exact change Delta, aspect, one undo step, shrinking from the top-left), Shift and limits, Escape cancels, Quill.js spelling kept and forced, left / centre / right / RTL geometry checked against the painted pixels with working handles, context menu, read-only / disabled / video, `resizeEmbed`; also at 2× and in the Fusion and Windows styles |
| `tst_flutter_parity::imageResizeExport` | records `quillqt_exports/image_resize.json` (style, style with other declarations on a centred line, Quill.js width/height) |
| flutter check | flutter_quill loads that export; `geometry_test.dart` renders the images at the written sizes (lines 164 / 184 / 84 = 160 / 180 / 80 + descent), and `tst_flutter_parity::geometry` matches quill-qt's layout to it |

### Deviations from the plan

- The tests are in a new `tst_image_resize` instead of `tst_embeds`.
- A press on a selected image (not on a handle) keeps it selected instead
  of starting Qt's drag-and-drop of the selection: images cannot be moved
  by dragging (cut and paste works).
- The drag preview is a tinted dashed rectangle from the fixed corner; a
  centred image grows from its corner in the preview and is centred again
  by the layout after the release.
- Percentages are of the natural image size; fit width is 100 % of the
  line.
