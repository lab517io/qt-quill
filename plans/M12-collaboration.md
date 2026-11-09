# M12 — Collaboration example

Estimate: 4–5 days. Depends on: M2 (transform, transformPosition, invert),
M5 (change Deltas, selection API), M6 (demo, toolbar), M11 (cost of applying
a remote change). Independent of M9.

## Goal

Show that quill-qt can be used for real-time collaborative editing, the way
Quill and flutter_quill are, and close the library gaps that stop this today:

1. Two or more `QuillEditor`s, in one process or on different machines,
   edit the same document concurrently and always **converge** to the same
   Delta.
2. **Undo/redo** stays local: Ctrl+Z undoes only the user's own edits, also
   after remote edits arrived (Quill's and flutter_quill's History do this by
   transforming the undo stack).
3. **Remote cursors and selections** are shown with each peer's name and
   colour.
4. The **wire format is plain Quill Delta JSON**, so a Quill.js or
   flutter_quill client could take part with a small adapter.

Not a goal: a production server (persistence, auth, scaling), CRDTs, offline
merge of long-diverged copies, or ShareDB protocol compatibility (see
"Decisions").

## What exists and what is missing (checked 2026-11-09)

| Item | State |
|---|---|
| `Delta::transform`, `transformPosition`, `invert`, `compose` | done in M2, oracle-checked against quill-delta 5.1.0 (M8) |
| `textChange(change, oldContents, source)` | emitted for every edit with a Quill-shaped change; remote changes go in through `updateContents(change, Source::Api)` |
| Selection kept across an API change | `updateContents` moves the local cursor with `transformPosition` |
| **Undo** | Qt's `QTextDocument` undo stack. Remote changes applied with `updateContents` end up on it, so Ctrl+Z would undo a peer's typing, and Qt's stack cannot be transformed. **Blocker.** |
| **Remote cursors** | nothing; `paintEvent` already paints decorations (checkboxes, code panel, quote bar), so an overlay fits there |
| Networking | QtNetwork available in Qt 6.4.2 / 6.8.3 / 6.12; **QtWebSockets is not** in Ubuntu's Qt install (an extra package) |

## Deliverables

```
src/delta/include/quill/OtClient.h     client state machine (Synchronized / AwaitingConfirm /
src/delta/OtClient.cpp                   AwaitingWithBuffer), pure QtCore, no I/O
src/delta/include/quill/OtServer.h     revision log: accepts (revision, delta), transforms it
src/delta/OtServer.cpp                   against newer revisions, returns the op to broadcast
src/widgets/include/quill/History.h    Delta-based undo/redo (Quill's History module semantics)
src/widgets/History.cpp
src/widgets/include/quill/RemoteCursors.h  peer cursors/selections painted over the editor
src/widgets/RemoteCursors.cpp
QuillEditor                            setHistory(...) / history(), remote-change entry point
apps/collab/                           quill-collab: server + client in one executable
  main.cpp                               --serve PORT | --connect HOST:PORT | --local N
  CollabSession.h/.cpp                   OtClient + QTcpSocket + editor glue
  CollabServer.h/.cpp                    OtServer + QTcpServer
  Protocol.h/.cpp                        line-delimited JSON messages
tests/delta/tst_ot_client.cpp          state machine + simulated network fuzz
tests/widgets/tst_history.cpp          ported Quill history tests + transform cases
tests/widgets/tst_collab.cpp           N editors through an in-memory server, random edits
tests/widgets/tst_remote_cursors.cpp   transform and painting of peer cursors
docs in README ("Collaboration"), plans/M12-collaboration.md outcome
```

## Design

### Consistency model: central server, client–server OT

The model Quill-based apps normally use (ShareDB, ot.js, Google Wave's
Jupiter variant). Only `transform` with a fixed tie-break is needed; it is
already oracle-checked, and this model does not need TP2.

- The **server** keeps the document, a revision number and the log of
  applied deltas. A client sends `(baseRevision, delta)`. The server
  transforms the delta against every log entry after `baseRevision`
  (`logEntry.transform(delta, true)`: the server's earlier op wins ties),
  composes it into the document, appends it and broadcasts it with the new
  revision; the sender gets an `ack`.
- The **client** (`OtClient`) is ot.js's three-state machine:
  - *Synchronized*: a local change is sent; go to AwaitingConfirm(sent).
  - *AwaitingConfirm(sent)*: further local changes are composed into a
    buffer; go to AwaitingWithBuffer(sent, buffer).
  - A remote op is transformed against `sent` (and `buffer`) before it is
    applied locally; `sent` and `buffer` are transformed in return.
  - `ack` advances the revision and sends the buffer, if any.
- `OtClient` is I/O-free: it has `applyLocal(delta)`, `applyRemote(rev,
  delta)`, `ack()` and callbacks `send(rev, delta)` / `apply(delta)`. That
  makes it fuzzable without sockets and reusable with any transport (a
  WebSocket, a Firebase-style store, a Qt Remote Objects channel).
- It lives in `QuillQt::Delta`, since it needs only Delta.

### Wire protocol (example only)

Line-delimited JSON over TCP, Delta ops in Quill.js JSON form (the same
JSON M2 reads and writes):

```
→ {"type":"hello","name":"Ann","color":"#e91e63"}
← {"type":"init","rev":12,"doc":[...ops...],"clientId":"c3","peers":[...]}
→ {"type":"op","rev":12,"delta":[...]}
← {"type":"ack","rev":13}
← {"type":"op","rev":14,"delta":[...],"clientId":"c1"}
→ {"type":"cursor","rev":14,"index":5,"length":0}
← {"type":"cursor","clientId":"c1","index":9,"length":3}
← {"type":"leave","clientId":"c1"}
```

TCP with QtNetwork rather than WebSockets, so the example builds with every
Qt install the project supports. The protocol is a few lines of
`QJsonDocument`; swapping in `QWebSocket` is one class.

### Applying remote changes in the editor

- `QuillEditor::updateContents(delta, Source::Api)` already applies a change
  and transforms the local selection. Check and pin it for the
  concurrent cases: a remote insert at the caret (the caret stays put, with
  Quill's tie-break), a remote delete that removes the selection, remote
  changes during an open IME composition (preedit), and during a mouse
  drag-selection.
- `textChange` with `Source::Api` is not sent back to the server; only
  `Source::User` changes go to `OtClient::applyLocal`. A host that applies
  its own API edits marks them as user changes or sends them explicitly; the
  example documents this choice.

### History (the blocker)

Replace Qt's undo stack with a Delta-based `quill::History`, a port of
Quill's `modules/history` with flutter_quill's options:

- Stack entries are `{undo, redo}` Delta pairs, built from user changes
  with `change.invert(oldContents)`; consecutive changes within `delay`
  (1000 ms) merge into one entry with `compose`; `maxStack` 100;
  `userOnly` true.
- On a non-user change, every stack entry is transformed against it
  (Quill's `transformStack`), so undo later applies only the local user's
  inverse at the right position. flutter_quill's History does the same.
- Undo/redo apply through `updateContents(.., Source::User)` and restore the
  selection Quill's way (`getLastChangeIndex`), so they produce change
  Deltas and go to the server like any other edit.
- `QuillEditor` gets `History* history()` and a choice of undo model.
  Two options; **recommended: Delta history only in collaborative mode**
  (`setHistory(std::make_unique<History>(...))`, otherwise Qt's stack as
  today) so M5–M8 behaviour and tests stay untouched. Installing a
  `History` disables `QTextDocument`'s undo, routes Ctrl+Z / Ctrl+Shift+Z /
  Ctrl+Y, the context menu and the toolbar's undo/redo actions through it,
  and emits `undoAvailable` / `redoAvailable` for the toolbar.
- The undo/redo detection at `QuillEditor.cpp:268` (Qt stack counts) is
  skipped while a `History` is installed.

### Remote cursors

- `RemoteCursors` stores `{clientId, name, colour, Range}` per peer and
  transforms every range with `transformPosition` on each text change (local
  or remote), so cursors do not lag until the next cursor message.
- Painted in `QuillEditor::paintEvent` after the text: a 2 px caret, a
  translucent selection (the colour at ~25 % alpha) and a name flag that
  appears on hover and for 2 s after movement; positions come from
  `cursorRect` / `QTextLayout` lines so wrapped and multi-line selections
  are right. Repaints only the affected rects.
- Hosts feed it from their own protocol: `setCursor(id, name, colour,
  range)` / `remove(id)`. Cursor messages are throttled (≤ 20/s).

### The example application

`quill-collab` reuses the demo's `MainWindow` pieces (toolbar, Delta
inspector) with a status line (connection, revision, peers, pending ops).

- `--serve 7777` runs a headless server (optionally `--doc file.json` as the
  start document).
- `--connect host:7777 --name Ann` opens a client window.
- `--local 3` starts an in-process server and three editor windows side by
  side: the one-command demo, and what the screenshot/smoke test runs.
- A debug panel with artificial latency (0–2000 ms) and a "go offline"
  toggle, so concurrent edits and buffering are easy to show by hand.
- Reconnect: on disconnect the client keeps editing in AwaitingWithBuffer;
  on reconnect it sends `hello` with its last revision, receives the missed
  ops and resends its pending op (the server dedupes by `(clientId, seq)`).

## Tasks

- [ ] `OtClient` + `OtServer` with unit tests (ot.js's test cases for the
      state machine, ported).
- [ ] Simulated-network fuzz: K clients, random local edits (reuse the M2
      random Delta generator against current contents), random delivery
      delays with per-connection FIFO; after draining, every client and the
      server equal the same Delta. Seeds fixed, 2000 runs in CI, longer runs
      by env var like the M5 fuzzers.
- [ ] `History`: port Quill's `test/unit/modules/history.ts` cases (delay
      merge, maxStack, userOnly, transform against api changes, selection
      after undo); add flutter_quill's history cases to the flutter check
      if its test suite has them.
- [ ] `QuillEditor::setHistory` wiring: keys, context menu, toolbar, signals;
      Qt undo disabled; all existing tests still pass with and without a
      `History` (run `tst_editor_keys` and `tst_toolbar` both ways).
- [ ] Pin `updateContents` selection behaviour for the concurrent cases
      above; fix what breaks (IME preedit is the likely one).
- [ ] `RemoteCursors` with transform tests and a painting test (grab the
      viewport, check the caret colour column, as `decorationsArePainted`
      does; HiDPI variant).
- [ ] `apps/collab`: protocol, server, client session, `--local N`, latency
      panel, reconnect.
- [ ] `tst_collab`: three editors + in-process server over an in-memory
      transport; random typing through `QTest` key events and API edits,
      random latency; converge; undo on one editor never removes another
      editor's text; with `QUILLQT_VERIFY=1`.
- [ ] Smoke test: `quill-collab --local 2 --screenshot` over real loopback
      TCP (offscreen), plus `ci/screenshot.sh` entry.
- [ ] Optional: `tools/collab-node/` — a Node client using `quill-delta`
      that joins the server and makes random edits, proving the wire format
      is plain Quill Delta (runs in the oracle CI job, Node is already there).
- [ ] README "Collaboration" section: model, the integration points
      (`OtClient`, `History`, `RemoteCursors`, which `Source` goes where),
      limits.

## Tests

| Test | What it proves |
|---|---|
| `tst_ot_client` | state machine transitions; fuzzed convergence of K clients + server |
| `tst_history` | Quill History semantics; undo after remote changes undoes only own edits |
| `tst_collab` | real editors converge, selections survive remote edits, no verification failures |
| `tst_remote_cursors` | peer ranges follow edits; painted where expected at 1× and 2× |
| `quill-collab-smoke` | the example runs end to end over TCP |
| optional Node client | a quill-delta client interoperates with the server |

## Exit criteria

- Fuzzed convergence passes (2000 seeded runs; 100 000 in a long run).
- `tst_collab` converges with 3 editors under random latency, 0 verification
  failures, on Qt 6.4.2, 6.8.3 and 6.12, ASan and clang configs.
- Undo in a collaborative editor never touches remote text.
- Existing M1–M9a tests unchanged and green; editors without a `History`
  behave exactly as before.
- `quill-collab --local 3` demonstrates typing, formatting, embeds and
  cursors live; the smoke test passes.

## Risks

- **Undo replacement is the large part.** Qt's stack is used by Ctrl+Z,
  the context menu, `QTextEdit::undo()` and the toolbar; missing one path
  undoes remote text silently. Mitigation: opt-in, plus a test that
  exercises every path with a remote change in between.
- **Selection after undo** differs between Qt and Quill; Quill's rules are
  ported, with tests, and differences listed.
- **IME composition** while a remote change arrives: Qt may commit or lose
  the preedit. If it cannot be done cleanly, buffer remote changes until the
  composition ends (Google Docs does similar).
- **Embeds in transform**: image/video embeds transform as length-1 inserts
  (no embed handlers needed); custom embed retains with object data would
  need handlers. The example uses only insert-type embeds.
- **Formats Quill attributes cannot express** (they do not exist in quill-qt
  by design), so nothing is lost on the wire.
- Performance: each remote op is `updateContents` plus a selection update;
  fine for typing rates. Large pastes from peers are the slow case, and a
  topic of the separate large-document item.

## Decisions (recommended defaults, change before starting)

1. **Server language**: C++/Qt inside `quill-collab` (recommended: one
   executable, no extra runtime) vs a Node server with `quill-delta`.
2. **Undo model**: Delta `History` opt-in for collaborative editors
   (recommended) vs replacing Qt's stack for every editor (closer to
   flutter_quill, but changes behaviour M5–M8 tested).
3. **Transport**: TCP + line-delimited JSON (recommended: no extra Qt module)
   vs QtWebSockets (browser clients could connect directly, adds a dependency).
4. **ShareDB compatibility**: not in M12 (recommended); the
   `OtClient`/`OtServer` split leaves room for a ShareDB adapter later.
