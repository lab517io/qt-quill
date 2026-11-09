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

- [ ] Confirm the attribute form from `flutter_quill_extensions`; write it
      down in the plan with the source location.
- [ ] Single-click image selection, frame and handles painted in
      `paintEvent`; hit testing; cursors.
- [ ] Drag with overlay preview, aspect ratio, Shift, limits, Escape;
      commit via `formatText`.
- [ ] Spelling rules (`style` vs `width`/`height`), other `style`
      properties kept.
- [ ] Context menu actions.
- [ ] `EmbedRenderer::resizable()`, `setImageResizeEnabled`, read-only.
- [ ] flutter-check: export a resized image from quill-qt, load it in
      flutter_quill, check the rendered size (with the M10 geometry probe).
- [ ] Demo: nothing to add beyond the feature; README "Embeds" paragraph.

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
