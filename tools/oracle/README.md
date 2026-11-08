# Reference oracles

The C++ Delta core must behave exactly like the JavaScript reference
(`quill-delta` / slab/delta, which flutter_quill's Dart port follows). The
scripts here produce reference data that is committed under
`tests/fixtures/delta/`, so the C++ test-suite runs without Node.js.

| Script | Output | What it records |
|---|---|---|
| `record-slab-tests.sh` | `slab-delta-recorded.json` | Every top-level Delta / AttributeMap call made by slab/delta's own Jasmine test-suite at a pinned commit: receiver, arguments, result or thrown error. Replayed by `tests/delta/tst_slab_recorded.cpp`. |
| `npm run fastdiff` | `fastdiff-cases.json` | fast-diff 1.3.0 outputs for its own regression and cursor cases plus seeded fuzz inputs (DNA, emoji, prose, embed placeholders). Replayed by `tests/delta/tst_fastdiff.cpp`. |

## Regenerating

Requires Node.js 18+, git and network access.

```sh
cd tools/oracle
npm install
npm run fastdiff
./record-slab-tests.sh            # clones slab/delta into a temp dir
DELTA_COMMIT=<sha> ./record-slab-tests.sh   # record a different upstream commit
```

Both outputs are deterministic. A diff in the committed files after
regenerating means the upstream behaviour changed; review it before
committing. Milestone M8 adds a CI job that regenerates and fails on drift.

## How the recorder works

`recorder-helper.cjs` is loaded as an extra Jasmine helper. It wraps the
Delta prototype methods and the AttributeMap functions; calls made while
another recorded call is running (internal calls, embed handler callbacks)
are not recorded. Values JSON cannot express are encoded as markers:
`{"$u":1}` for undefined, `{"$inf":1}` for Infinity, `{"$delta":[...]}` for a
Delta argument or result, `{"$self":1}` for builder methods returning the
receiver. The run fails loudly if any upstream spec fails, so a recording is
never made from a broken reference.
