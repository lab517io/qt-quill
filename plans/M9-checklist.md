# M9 manual checklist (run on Windows and on macOS)

Prepared in M9a. Everything the test-suite covers is not repeated here: the
CI jobs `build-windows` and `build-macos` run ctest offscreen. This list is
what needs a real display, keyboard, clipboard and other applications.

Setup: build Release, start `quill-demo --sample showcase.json`. Record the
OS version, Qt version, display scale and the result of each item in the M9
outcome.

## Typing and keyboard

- [ ] Typing, Enter, Backspace, Delete, arrows, Home/End, word jumps
      (Ctrl+arrows on Windows, Option+arrows on macOS).
- [ ] Bold / italic / underline shortcuts: Ctrl+B/I/U on Windows, Cmd+B/I/U on
      macOS; strike with Ctrl+Shift+X / Cmd+Shift+X; link with Ctrl+K / Cmd+K.
- [ ] Ctrl+click (Cmd+click on macOS) on a link emits a link activation.
- [ ] `1. ` / `- ` / `[] ` start lists; Enter continues, Enter on an empty
      item ends the list; Tab at an item start indents, Shift+Tab outdents.
- [ ] Undo / redo (Ctrl+Z / Ctrl+Y or Ctrl+Shift+Z; Cmd+Z / Cmd+Shift+Z).
- [ ] Input methods: accents with dead keys (Windows US-International, macOS
      Option+e then e), long-press accents on macOS, Japanese IME with
      composition (candidate window position follows the caret; the change
      Delta appears only after committing).
- [ ] Emoji picker (Win+. / Ctrl+Cmd+Space); emoji count as two UTF-16 units
      in the status bar index.

## Mouse

- [ ] Click a checklist box toggles it, at 100 % and at 150 % / 200 % display
      scale.
- [ ] Double click an image emits embedActivated; click a video / formula.
- [ ] Drag text within the editor (move) and with Ctrl / Option (copy).

## Clipboard and drag and drop

- [ ] Copy from quill-demo and paste into quill-demo keeps all formats
      (Delta clipboard format).
- [ ] Paste from Word (Windows) / Pages (macOS): headings, bold, italic,
      bullet and numbered lists, nested list level.
- [ ] Paste from Edge / Safari / Chrome: headings, links, inline code, lists,
      quote, code block; no font, size or colour leaking in.
- [ ] Paste from Google Docs in a browser: no all-bold text, lists kept.
- [ ] Paste plain text with Windows line endings (Notepad) gives single line
      breaks.
- [ ] Paste a screenshot (image on the clipboard) inserts an image.
- [ ] Drag an image file from Explorer / Finder into the editor.
- [ ] Copy from quill-demo and paste into Word / Pages / a browser text area
      gives sensible rich text / plain text.
- [ ] Capture each paste above (clipboard HTML) into tests/fixtures/paste/,
      replacing the hand-built fixtures; update the expected Deltas.

## Rendering

- [ ] Fonts: `monospace`, `serif`, `sans-serif` samples use a fixed-width,
      serif and sans-serif font; code blocks use the system fixed font.
- [ ] Headings, lists, quote bar, code panel and spacing look like on Linux
      (compare with `ci/screenshot.sh` output).
- [ ] System dark mode (Windows "Dark" app mode, macOS Dark appearance) and
      View > Dark palette: toolbar icons, quote bar, code panel readable.
- [ ] HiDPI monitor (Retina, 150 % / 200 % Windows scaling): icons sharp,
      decorations aligned, images not blurry at their natural size.
- [ ] A remote image (https) loads; an invalid certificate shows a broken
      image without hanging (TLS backends: Schannel, SecureTransport).
- [ ] Image paths: a document with `C:\\Users\\...\\pic.png` (Windows) and
      `/Users/.../pic.png` (macOS) shows the images.

## Files and deployment

- [ ] Open / Save / Save As with native dialogs; paths with spaces and
      non-ASCII characters.
- [ ] `windeployqt quill-demo.exe` / `macdeployqt quill-demo.app` produce a
      demo that starts on a machine without Qt installed.
