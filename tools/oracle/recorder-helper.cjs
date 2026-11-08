// Jasmine helper that records every top-level call into slab/delta made by
// slab/delta's own test-suite: receiver, arguments, result or thrown error.
// The recording is replayed by tests/delta/tst_slab_recorded.cpp so the C++
// port is checked against exactly what the reference tests exercise.
//
// Loaded after jasmine-typescript-helper.js (ts-node). Environment:
//   DELTA_DIR      checkout of slab/delta
//   RECORD_OUTPUT  output JSON path
'use strict';
const fs = require('fs');
const path = require('path');

const DELTA_DIR = process.env.DELTA_DIR;
// Jasmine may load this helper before jasmine-typescript-helper.js; make sure
// TypeScript sources can be required (ts-node ignores a second registration).
if (!process[Symbol.for('ts-node.register.instance')]) {
  require(path.join(DELTA_DIR, 'node_modules', 'ts-node')).register({
    project: path.join(DELTA_DIR, 'tsconfig.json'),
  });
}
const Delta = require(path.join(DELTA_DIR, 'src', 'Delta'));
const AttributeMap = Delta.AttributeMap;

let currentSpec = null;
let depth = 0;
let seq = 0;
const activeHandlers = new Set();
const records = [];
const specResults = [];

function enc(v) {
  if (v === undefined) return { $u: 1 };
  if (v === Infinity) return { $inf: 1 };
  if (v instanceof Delta) return { $delta: enc(v.ops) };
  if (typeof v === 'function') return { $fn: 1 };
  if (Array.isArray(v)) return v.map(enc);
  if (v !== null && typeof v === 'object') {
    const o = {};
    for (const k of Object.keys(v)) if (v[k] !== undefined) o[k] = enc(v[k]);
    return o;
  }
  return v;
}

function record(kind, name, self, args, run) {
  if (depth > 0 || currentSpec === null) return run(args);
  const rec = {
    spec: currentSpec,
    seq: seq++,
    kind,
    method: name,
    handlers: [...activeHandlers].sort(),
  };
  if (self !== undefined) rec.self = enc(self.ops);
  const calls = [];
  const wrappedArgs = args.map((a) => {
    if (typeof a !== 'function') return a;
    // Callbacks run while depth > 0, so Delta calls they make are not recorded.
    return function (...cbArgs) {
      const ret = a.apply(this, cbArgs);
      calls.push({ args: cbArgs.map(enc), ret: enc(ret) });
      return ret;
    };
  });
  rec.args = args.map(enc);
  depth++;
  try {
    const result = run(wrappedArgs);
    rec.result = self !== undefined && result === self ? { $self: 1 } : enc(result);
    if (self !== undefined) rec.selfAfter = enc(self.ops);
    return result;
  } catch (e) {
    rec.throws = String(e && e.message);
    throw e;
  } finally {
    depth--;
    if (calls.length || args.some((a) => typeof a === 'function')) rec.calls = calls;
    records.push(rec);
  }
}

const methods = ['insert', 'delete', 'retain', 'push', 'chop', 'concat', 'compose',
  'transform', 'transformPosition', 'diff', 'invert', 'slice', 'length',
  'changeLength', 'eachLine'];
for (const m of methods) {
  const orig = Delta.prototype[m];
  Delta.prototype[m] = function (...args) {
    return record('delta', m, this, args, (a) => orig.apply(this, a));
  };
}
for (const m of ['compose', 'diff', 'invert', 'transform']) {
  const orig = AttributeMap[m];
  AttributeMap[m] = function (...args) {
    return record('attributes', m, undefined, args, (a) => orig.apply(this, a));
  };
}
const origRegister = Delta.registerEmbed;
Delta.registerEmbed = function (type, handler) {
  activeHandlers.add(type);
  return origRegister.call(this, type, handler);
};
const origUnregister = Delta.unregisterEmbed;
Delta.unregisterEmbed = function (type) {
  activeHandlers.delete(type);
  return origUnregister.call(this, type);
};

jasmine.getEnv().addReporter({
  specStarted(r) {
    currentSpec = r.fullName;
    seq = 0;
  },
  specDone(r) {
    specResults.push({ spec: r.fullName, status: r.status });
    currentSpec = null;
  },
  jasmineDone() {
    const failed = specResults.filter((s) => s.status !== 'passed');
    const out = {
      generator: 'tools/oracle/record-slab-tests.sh',
      source: 'https://github.com/slab/delta',
      commit: process.env.DELTA_COMMIT || 'unknown',
      specs: specResults.length,
      failedSpecs: failed.map((s) => s.spec),
      records,
    };
    fs.writeFileSync(process.env.RECORD_OUTPUT, JSON.stringify(out, null, 1) + '\n');
    console.log(`recorded ${records.length} calls from ${specResults.length} specs`);
  },
});
