// OT oracle (milestone M8): evaluates every case in tests/fixtures/ot/cases/
// with the pinned quill-delta and writes tests/fixtures/ot/expected/.
//
//   node generate.mjs                       evaluate all case files
//   node generate.mjs --random 500 --seed 42  also (re)write cases/random.json
//
// Case: {name, op, a, b?, priority?, index?, cursor?, start?, end?, newline?}
// with op one of compose, transform, transformPosition, diff, invert, slice,
// concat, eachLine, length, changeLength. Expected: {name: {result} | {error}}.
// Output is deterministic; tests/delta/tst_oracle.cpp replays it.
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const Delta = require('quill-delta');
const here = dirname(fileURLToPath(import.meta.url));
const root = join(here, '..', '..', 'tests', 'fixtures', 'ot');

function arg(name, fallback) {
  const i = process.argv.indexOf(name);
  return i >= 0 ? process.argv[i + 1] : fallback;
}

// --- Evaluation ----------------------------------------------------------------

export function evaluate(c) {
  const a = new Delta(c.a);
  const b = c.b ? new Delta(c.b) : undefined;
  switch (c.op) {
    case 'compose': return a.compose(b).ops;
    case 'transform': return a.transform(b, !!c.priority).ops;
    case 'transformPosition': return a.transformPosition(c.index, !!c.priority);
    case 'diff': return a.diff(b, c.cursor ?? undefined).ops;
    case 'invert': return a.invert(b).ops;
    case 'slice': return a.slice(c.start ?? 0, c.end ?? Infinity).ops;
    case 'concat': return a.concat(b).ops;
    case 'length': return a.length();
    case 'changeLength': return a.changeLength();
    case 'eachLine': {
      const lines = [];
      a.eachLine((line, attributes, index) => {
        lines.push({ line: line.ops, attributes, index });
      }, c.newline ?? '\n');
      return lines;
    }
    default: throw new Error(`unknown op ${c.op}`);
  }
}

function run(c) {
  try {
    return { result: evaluate(c) };
  } catch (e) {
    return { error: String(e.message ?? e) };
  }
}

// --- Random cases ----------------------------------------------------------------

function prng(seed) { // mulberry32
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function randomCases(count, seed) {
  const random = prng(seed);
  const int = (n) => Math.floor(random() * n);
  const pick = (arr) => arr[int(arr.length)];
  const chance = (p) => random() < p;
  const chars = ['a', 'b', 'c', ' ', '\n', '\n', '😀', 'é', 'ß'];
  const inlineAttrs = () => {
    const out = {};
    if (chance(0.3)) out.bold = true;
    if (chance(0.2)) out.italic = true;
    if (chance(0.2)) out.color = pick(['red', '#00ff00', '#FFE53935']);
    return Object.keys(out).length ? out : undefined;
  };
  const changeAttrs = () => {
    const out = {};
    if (chance(0.3)) out.bold = pick([true, null]);
    if (chance(0.2)) out.color = pick(['blue', null]);
    if (chance(0.15)) out.header = pick([1, 2, null]);
    if (chance(0.15)) out.list = pick(['bullet', 'ordered', null]);
    return Object.keys(out).length ? out : undefined;
  };
  const text = () => Array.from({ length: 1 + int(5) }, () => pick(chars)).join('');
  const embed = () => pick([{ image: 'i.png' }, { video: 'v' }, { formula: 'x^2' }]);

  const randomDoc = () => {
    const d = new Delta();
    const n = 1 + int(6);
    for (let i = 0; i < n; i++) {
      if (chance(0.15)) d.insert(embed(), inlineAttrs());
      else d.insert(text(), inlineAttrs());
      if (chance(0.3)) d.insert('\n', pick([{ header: 1 }, { list: 'bullet' }, { align: 'center' }, undefined]));
    }
    d.insert('\n');
    return d;
  };
  // Code unit offsets that do not split a surrogate pair.
  const boundaries = (doc) => {
    const set = new Set([0]);
    let pos = 0;
    for (const op of doc.ops) {
      if (typeof op.insert === 'string') {
        for (const ch of op.insert) { pos += ch.length; set.add(pos); }
      } else { pos += 1; set.add(pos); }
    }
    return set;
  };
  const randomChange = (doc) => {
    const length = doc.length();
    const ok = boundaries(doc);
    const step = (pos) => {
      let end = pos + 1 + int(4);
      while (end < length && !ok.has(end)) end++;
      return Math.min(end, length) - pos;
    };
    const d = new Delta();
    let pos = 0;
    while (pos < length && chance(0.85)) {
      const r = random();
      if (r < 0.45) { const k = step(pos); d.retain(k, chance(0.4) ? changeAttrs() : undefined); pos += k; }
      else if (r < 0.7) { const k = step(pos); d.delete(k); pos += k; }
      else if (r < 0.9) d.insert(text(), inlineAttrs());
      else d.insert(embed(), inlineAttrs());
    }
    if (chance(0.2)) d.insert(text());
    return d;
  };

  const cases = [];
  for (let i = 0; i < count; i++) {
    const p = `random/${i}/`;
    const A = randomDoc();
    const X = randomChange(A);
    const Y = randomChange(A);
    const AX = A.compose(X);
    const Z = randomChange(AX);
    const prio = chance(0.5);
    cases.push({ name: p + 'compose doc', op: 'compose', a: A.ops, b: X.ops });
    cases.push({ name: p + 'compose changes', op: 'compose', a: X.ops, b: Z.ops });
    cases.push({ name: p + 'transform', op: 'transform', a: X.ops, b: Y.ops, priority: prio });
    cases.push({ name: p + 'transformPosition', op: 'transformPosition', a: X.ops, index: int(A.length() + 1), priority: prio });
    cases.push({ name: p + 'invert', op: 'invert', a: X.ops, b: A.ops });
    cases.push({ name: p + 'diff', op: 'diff', a: A.ops, b: AX.ops });
    cases.push({ name: p + 'eachLine', op: 'eachLine', a: AX.ops });
    const s = int(AX.length() + 1);
    cases.push({ name: p + 'slice', op: 'slice', a: AX.ops, start: s, end: s + int(6) });
  }
  return cases;
}

// --- Main ------------------------------------------------------------------------

// One case per line: small files, readable diffs.
const jsonLines = (items) => '[\n' + items.map((v) => JSON.stringify(v)).join(',\n') + '\n]\n';
const jsonMap = (obj) =>
  '{\n' + Object.entries(obj).map(([k, v]) => `${JSON.stringify(k)}: ${JSON.stringify(v)}`).join(',\n') + '\n}\n';
const randomCount = arg('--random');
if (randomCount) {
  const cases = randomCases(Number(randomCount), Number(arg('--seed', '42')));
  writeFileSync(join(root, 'cases', 'random.json'), jsonLines(cases));
}
for (const file of readdirSync(join(root, 'cases')).filter((f) => f.endsWith('.json')).sort()) {
  const cases = JSON.parse(readFileSync(join(root, 'cases', file), 'utf8'));
  const expected = {};
  for (const c of cases) {
    if (c.name in expected) throw new Error(`duplicate case name ${c.name}`);
    expected[c.name] = run(c);
  }
  writeFileSync(join(root, 'expected', file), jsonMap(expected));
  console.log(`${file}: ${cases.length} cases`);
}
