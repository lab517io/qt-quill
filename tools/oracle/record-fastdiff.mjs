// Generates tests/fixtures/delta/fastdiff-cases.json: inputs and the exact
// output of fast-diff 1.3.0 (the version quill-delta 5.1.0 uses). Inputs are
// deterministic (seeded PRNG), so regenerating only changes the file when
// fast-diff's behaviour changes.
import { writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const diff = require('fast-diff');
const out = process.argv[2];
if (!out) throw new Error('usage: node record-fastdiff.mjs <output.json>');

// mulberry32
function prng(seed) {
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const random = prng(20261108);
const pick = (arr) => arr[Math.floor(random() * arr.length)];
const randomString = (alphabet, maxLength) =>
  Array.from({ length: Math.floor(random() * (maxLength + 1)) }, () => pick(alphabet)).join('');

const cases = [];
const add = (group, a, b, cursor) => {
  for (const cleanup of [false, true]) {
    const result = diff(a, b, cursor, cleanup);
    cases.push({ group, a, b, cursor: cursor === undefined ? null : cursor, cleanup, result });
  }
};

// fast-diff's own regression pairs.
[
  ['GAATAAAAAAAGATTAACAT', 'AAAAACTTGTAATTAACAAC'],
  ['🔘🤘🔗🔗', '🔗🤗🤗__🤗🤘🤘🤗🔗🤘🔗'],
  ['🔗🤗🤗__🤗🤘🤘🤗🔗🤘🔗', '🤗🤘🔘'],
  ['🤘🤘🔘🔘_🔘🔗🤘🤗🤗__🔗🤘', '🤘🔘🤘🔗🤘🤘🔗🤗🤘🔘🔘'],
  ['🤗🤘🤗🔘🤘🔘🤗_🤗🔗🤘🤗_🤘🔗🤗🤘🔗🤘🤘🤘🔗🤗🔗🔗🔗🤗_🤘🔗🤗🤗🔘🤗🤗🤘🤗',
   '_🤗🤘_🤘🤘🔘🤗🔘🤘_🔘🤗🔗🔘🔗🤘🔗🤘🤗🔗🔗🔗🤘🔘_🤗🤘🤘🤘__🤘_🔘🤘🤘_🔗🤘🔘'],
  ['🔗🤘🤗🔘🔘🤗', '🤘🤘🤘🤗🔘🔗🔗'],
  ['🔘_🔗🔗🔗🤗🔗', '🤘🤗🔗🤗_🤘🔘_'],
].forEach(([a, b]) => add('regression', a, b));

// fast-diff's cursor test inputs, plain and with an "x" prefix/suffix.
const cursorCases = [
  ['', 0, '', null], ['', 0, 'a', null], ['a', 0, 'aa', null], ['a', 1, 'aa', null],
  ['aa', 0, 'aaa', null], ['aa', 1, 'aaa', null], ['aa', 2, 'aaa', null],
  ['aaa', 0, 'aaaa', null], ['aaa', 1, 'aaaa', null], ['aaa', 2, 'aaaa', null],
  ['aaa', 3, 'aaaa', null], ['a', 0, '', null], ['a', 1, '', null], ['aa', 0, 'a', null],
  ['aa', 1, 'a', null], ['aa', 2, 'a', null], ['aaa', 0, 'aa', null], ['aaa', 1, 'aa', null],
  ['aaa', 2, 'aa', null], ['aaa', 3, 'aa', null], ['', 0, '', 0], ['', 0, 'a', 1],
  ['a', 0, 'aa', 1], ['a', 1, 'aa', 2], ['aa', 0, 'aaa', 1], ['aa', 1, 'aaa', 2],
  ['aa', 2, 'aaa', 3], ['aaa', 0, 'aaaa', 1], ['aaa', 1, 'aaaa', 2], ['aaa', 2, 'aaaa', 3],
  ['aaa', 3, 'aaaa', 4], ['a', 1, '', 0], ['aa', 1, 'a', 0], ['aa', 2, 'a', 1],
  ['aaa', 1, 'aa', 0], ['aaa', 2, 'aa', 1], ['aaa', 3, 'aa', 2], ['aaa', 3, 'aa', 0],
  ['12345', [1, 2], '15', 1], ['12345', [1, 2], 'a545', 1], ['a', 0, '', 0],
  ['aa', 0, 'a', 0], ['aa', 1, 'a', 1], ['aaa', 0, 'aa', 0], ['aaa', 1, 'aa', 1],
  ['aaa', 2, 'aa', 2], ['bob', 0, 'bobob', null], ['bob', 1, 'bobob', null],
  ['bob', 2, 'bobob', null], ['bob', 3, 'bobob', null], ['bob', 0, 'bobob', 2],
  ['bob', 1, 'bobob', 3], ['bob', 2, 'bobob', 4], ['bob', 3, 'bobob', 5],
  ['bobob', 2, 'bob', null], ['bobob', 3, 'bob', null], ['bobob', 4, 'bob', null],
  ['bobob', 5, 'bob', null], ['bobob', 2, 'bob', 0], ['bobob', 3, 'bob', 1],
  ['bobob', 4, 'bob', 2], ['bobob', 5, 'bob', 3], ['bob', 1, 'b', null],
  ['hello', [0, 5], 'h', 1], ['yay', [0, 3], 'y', 1], ['bobob', [1, 4], 'bob', 2],
];
const toCursor = (oldSpec, newSpec, shift) => {
  const oldRange = typeof oldSpec === 'number'
    ? { index: oldSpec + shift, length: 0 }
    : { index: oldSpec[0] + shift, length: oldSpec[1] - oldSpec[0] };
  if (newSpec === null) return oldRange.index;
  return { oldRange, newRange: { index: newSpec + shift, length: 0 } };
};
for (const [a, oldSpec, b, newSpec] of cursorCases) {
  add('cursor', a, b, toCursor(oldSpec, newSpec, 0));
  add('cursor', 'x' + a, 'x' + b, toCursor(oldSpec, newSpec, 1));
  add('cursor', a + 'x', b + 'x', toCursor(oldSpec, newSpec, 0));
}

// Seeded fuzz: DNA alphabet (exercises bisect and half-match), with and
// without a random cursor.
for (let i = 0; i < 150; i++) {
  const a = randomString(['G', 'A', 'T', 'T', 'A', 'C', 'A'], 60);
  const b = randomString(['G', 'A', 'T', 'T', 'A', 'C', 'A'], 60);
  add('fuzz-dna', a, b);
  add('fuzz-dna-cursor', a, b, Math.floor(random() * (a.length + 1)));
}

// Seeded fuzz: emoji (surrogate pairs) mixed with ASCII.
for (let i = 0; i < 100; i++) {
  const alphabet = ['🔘', '🤘', '🔗', '🤗', '_', 'a', '\n'];
  add('fuzz-emoji', randomString(alphabet, 25), randomString(alphabet, 25));
}

// Prose edits: semantic cleanup and lossless shifting on word boundaries.
const words = ['the', 'quick', 'brown', 'fox', 'jumps', 'over', 'lazy', 'dog', 'Quill',
  'delta', 'editor', 'text,', 'line.', '\n', '\n\n', ' '];
for (let i = 0; i < 100; i++) {
  const base = Array.from({ length: 4 + Math.floor(random() * 12) }, () => pick(words)).join(' ');
  const parts = base.split(' ');
  const edits = 1 + Math.floor(random() * 3);
  for (let e = 0; e < edits; e++) {
    const at = Math.floor(random() * (parts.length + 1));
    const kind = random();
    if (kind < 0.4) parts.splice(at, 0, pick(words));
    else if (kind < 0.7) parts.splice(at, 1);
    else parts.splice(at, 1, pick(words));
  }
  add('prose', base, parts.join(' '));
}

// Embed placeholders as Delta.diff() builds them.
for (let i = 0; i < 50; i++) {
  const alphabet = ['a', 'b', '\0', '\n', ' '];
  add('embeds', randomString(alphabet, 20), randomString(alphabet, 20));
}

writeFileSync(out, JSON.stringify({
  generator: 'tools/oracle/record-fastdiff.mjs',
  fastDiffVersion: require('fast-diff/package.json').version,
  cases,
}) + '\n');
console.log(`wrote ${cases.length} cases to ${out}`);
