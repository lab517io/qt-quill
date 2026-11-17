// A Node.js client for the quill-collab server, written against the
// JavaScript quill-delta package. It joins over TCP, makes random edits with
// its own client-side OT (the same three states as quill::OtClient), waits
// until everything is acknowledged and prints its document as JSON.
//
//   node client.mjs --port 7777 [--host 127.0.0.1] [--name js1] [--edits 50] [--seed 1]
//
// --edits 0 only joins and prints the server's document.
import net from 'node:net';
import Delta from 'quill-delta';

const args = Object.fromEntries(
  process.argv.slice(2).reduce((pairs, a, i, all) => {
    if (a.startsWith('--')) pairs.push([a.slice(2), all[i + 1]]);
    return pairs;
  }, []),
);
const port = Number(args.port);
const host = args.host ?? '127.0.0.1';
const name = args.name ?? 'node';
const edits = Number(args.edits ?? 50);
let seed = Number(args.seed ?? 1);

function random() {
  // mulberry32: reproducible runs
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const below = (n) => Math.floor(random() * Math.max(1, n));

let doc = new Delta();
let revision = 0;
let sequence = 0;
let outstanding = null;
let buffer = null;
let clientId = null;
let made = 0;
let lastMessage = Date.now();

const socket = net.connect(port, host);
let pending = '';
const send = (m) => socket.write(JSON.stringify(m) + '\n');

function sendOutstanding() {
  send({ type: 'op', rev: revision, seq: sequence, delta: outstanding.ops });
}

function applyClient(change) {
  doc = doc.compose(change);
  if (!outstanding) {
    outstanding = change;
    sequence += 1;
    sendOutstanding();
  } else if (!buffer) {
    buffer = change;
  } else {
    buffer = buffer.compose(change);
  }
}

function applyServer(change) {
  revision += 1;
  if (outstanding) {
    const o = outstanding;
    outstanding = change.transform(o, true);
    change = o.transform(change, false);
  }
  if (buffer) {
    const b = buffer;
    buffer = change.transform(b, true);
    change = b.transform(change, false);
  }
  doc = doc.compose(change);
}

function serverAck() {
  revision += 1;
  outstanding = buffer;
  buffer = null;
  if (outstanding) {
    sequence += 1;
    sendOutstanding();
  }
}

function randomEdit() {
  const length = doc.length() - 1; // the final newline stays
  const at = below(length + 1);
  const change = new Delta().retain(at);
  const kind = below(10);
  if (kind < 5 || at === length) {
    const letters = 'jsnode';
    let text = '';
    for (let i = 1 + below(4); i > 0; i -= 1) text += letters[below(letters.length)];
    if (below(6) === 0) text += '\n';
    change.insert(text, below(4) === 0 ? { italic: true } : undefined);
  } else if (kind < 8) {
    change.delete(1 + below(Math.min(3, length - at)));
  } else {
    change.retain(1 + below(Math.min(5, length - at)), { bold: below(2) ? true : null });
  }
  applyClient(change.chop());
}

socket.on('connect', () => send({ type: 'hello', name, color: '#607d8b' }));
socket.on('data', (data) => {
  pending += data.toString('utf8');
  let nl;
  while ((nl = pending.indexOf('\n')) >= 0) {
    const line = pending.slice(0, nl);
    pending = pending.slice(nl + 1);
    if (!line.trim()) continue;
    const m = JSON.parse(line);
    lastMessage = Date.now();
    switch (m.type) {
      case 'init':
        doc = new Delta(m.doc);
        revision = m.rev;
        clientId = m.clientId;
        break;
      case 'ack':
        serverAck();
        break;
      case 'op':
        applyServer(new Delta(m.delta));
        break;
      case 'error':
        console.error(`server error: ${m.message}`);
        process.exit(2);
        break;
      default:
        break; // cursor, join, leave
    }
  }
});
socket.on('error', (e) => {
  console.error(`connection failed: ${e.message}`);
  process.exit(2);
});

const timer = setInterval(() => {
  if (!clientId) return;
  if (made < edits) {
    randomEdit();
    made += 1;
    return;
  }
  // Done editing: wait until acknowledged and quiet.
  if (!outstanding && Date.now() - lastMessage > 400) {
    clearInterval(timer);
    // Canonical key order, so documents compare as text.
    const canonical = doc.ops.map((op) => {
      const out = { insert: op.insert };
      if (op.attributes) {
        out.attributes = Object.fromEntries(Object.keys(op.attributes).sort().map((k) => [k, op.attributes[k]]));
      }
      return out;
    });
    process.stdout.write(JSON.stringify(canonical) + '\n');
    socket.end();
  }
}, 5 + below(10));
