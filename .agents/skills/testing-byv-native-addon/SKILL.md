---
name: testing-byv-native-addon
description: How to build, run and adversarially test the byv-storage native Node-API C++ addon (BYV v7 binary format) — toolchain pitfalls, differential builds, crafting hostile binary payloads, and the BYV language grammar.
---

# Testing the `byv-storage` native addon

`byv-storage` is a Node-API (node-addon-api) C++ addon implementing the BYV v7 binary serialization
format. There is **no HTTP server, no UI and no frontend** — all testing is Node/CLI. Do not start a
browser or a screen recording for this repo; collect command output as text evidence instead.

## Toolchain

- **Use Node 22.** `export PATH=~/.nvm/versions/node/v22.12.0/bin:$PATH`. On a default Node 20 box,
  `node-gyp` 13 may crash with `webidl.util.markAsUncloneable is not a function`; if you see that,
  it is a toolchain problem, not a repo problem. The repo blueprint pins Node 22 via `setup-node`.
- Build: `npx node-gyp rebuild` (or `npx node-gyp build` for an incremental rebuild). ~40 s.
- Tests: `npm test` → `node --test "test/**/*.test.mjs"`.
- Public API (identical for CJS `index.cjs` and ESM `index.js`, both re-exporting `native.cjs`):
  `serialize`, `serializeFast`, `deserialize`, `deserializeFast`, `parse`, `compile`, `inspect`,
  `version`. Load a specific build from an out-of-repo harness with
  `createRequire(import.meta.url)("/abs/path/to/index.cjs")`.
- `native.cjs` is a thin loader/validation wrapper: it checks all 8 exports exist and wraps addon
  load failure in an `Error` with a `cause`. It does not change error classes at runtime.

## Golden rule: build a differential

A hardening or bugfix test that only runs against the patched build proves nothing. Always build the
"change reverted" variant and run the *same* harness against both:

```bash
git worktree add /home/ubuntu/testing/mainwt origin/main
cp -r node_modules /home/ubuntu/testing/mainwt/        # avoids a second npm install
cd /home/ubuntu/testing/mainwt && npx node-gyp rebuild
```

A worktree at the merge base (or at `origin/main`) is usually a better differential than hand-reverting
a hunk. Copying the repo's `node_modules` in is enough — no `npm install` needed. If the harness
behaves identically on both builds, report the test as inconclusive rather than as a pass.

You can also copy a PR's new test file into the differential worktree and run
`node --test test/<file>` there — "fails on main, passes on the branch" is strong, cheap evidence.

## Crash-class testing

The whole point of hardening tests is distinguishing a catchable JS `Error` from a process death.
Run **every** hostile case in its own child process and inspect `status`/`signal` from
`spawnSync`, never just a try/catch in the harness process:

- `signal === "SIGSEGV"` → stack overflow / unbounded recursion.
- `FATAL ERROR: Reached heap limit` on stderr with `status !== 0` → uncatchable OOM.
- Classify every caught throw with `e instanceof Error`; a thrown non-`Error` is a defect too.
- Beware false positives from your own harness: building deeply indented sources with quadratic
  indentation hits `RangeError: Invalid string length` in **JS** around depth ~50k. Verify a
  suspicious "crash" is the addon and not the harness before reporting it.
- Keep harness scripts **outside** the repo (e.g. `/home/ubuntu/testing/`) so the working tree stays clean.

## Crafting hostile binary payloads

Reuse `test/helpers/binary.mjs` — it exports `legacy`, `packed`, `withHeader(flags, payload)`,
`withPackedHeader(flags, keys, strings, value)` and `varUInt`. Copy it next to your harness.

Header: bytes 0-3 magic `BYV7`, byte 4 version `7`, byte 5 flags, bytes 6-9 little-endian payload size.
Flags: `0x01` packed keys, `0x02` string dictionary (**only valid together with `0x01`**), `0x03` both;
any other bit is rejected. Type bytes: 0 null, 1 bool, 2 int64, 3 double, 4 string, 5 object, 6 array.
Legacy object entries are `u32 keyLen + key + value`; packed object entries are `varUInt keyId + value`;
packed strings are `[4, marker]` where marker 0 = inline and 1 = dictionary ref.

High-yield hostile input families (all should throw a catchable `Error`):
declared counts that the remaining bytes cannot pay for; counts above the 1e6 cap; every truncation
prefix `0..N` (also with the declared payload size corrected, so you exercise the reader and not just
the size check); invalid type bytes 7-255 at root/nested/packed positions; dictionary key ids and
string-dictionary ref ids out of range; invalid string markers; 10-byte `0xFF` varints; declared payload
size larger/smaller than actual; trailing garbage; zero-length and header-only (10-byte) buffers;
byte-flip/insert/delete/truncate mutations of valid payloads. Drive `inspect` with the same buffers —
note `inspect` returns a descriptor instead of throwing for most malformed input, so ~1/3 of your
"returned" count is expected to be `inspect`.

## Prototype-pollution testing

`Set("__proto__", v)` invokes the inherited setter and replaces the *decoded object's* prototype; the
fix installs such keys with `DefineProperty`. Note that `Object.prototype` itself is **never** mutated
either way, so a global `({}).polluted === undefined` probe will pass on a vulnerable build — assert
per-object instead:

- `Object.getPrototypeOf(result) === Object.prototype`
- `Object.getOwnPropertyDescriptor(result, "__proto__")` exists with `writable/enumerable/configurable`
  all `true` and the expected `value`
- `Object.keys`, `JSON.stringify`, spread and `for...in` all see the key

Cover the cases a naive fix would miss: the key at a **non-zero** and **duplicated** index in the packed
key dictionary (catches a hardcoded index-0 check); `__proto__` whose value is a **primitive or array**
(`Set` silently no-ops, so the prototype looks fine but the property is missing); the key nested deep
inside arrays/objects; the key repeated in one object body (last-wins); and `constructor` / `prototype`
/ `toString` / `valueOf` / `hasOwnProperty` keys, which must stay ordinary own properties.
There are three decode sites to cover — `toJS` (used by `deserialize`), `readFastLegacyValue` and
`readFastPackedValue` (both used by `deserializeFast`) — so always test **both** decoders against
**both** legacy (flags 0x00) and packed (flags 0x01/0x03) payloads.

## BYV language grammar (for `parse` / `compile`)

Easy to get wrong; a bad guess produces `BYV lexer error ... Unexpected character` and silently fails to
exercise the parser. Blocks use `::`, scalars use `->`, array items are `-` prefixed, indentation is
significant, and an optional `@BYV` banner may lead the file:

```
@BYV

status -> "success"
code -> 200

data ::
    id -> 42
    tags ::
        -"binary"
        -"native"
```

Invariants worth asserting: `parse(src)` deep-equals `deserialize(compile(src))` and
`deserializeFast(compile(src))`, and `compile(src)` is byte-identical to `serialize(parse(src))`.
See `test/language.test.mjs` and `examples/language.ts` for the canonical source.

## Regression checklist

- All four combinations `serialize|serializeFast` × `deserialize|deserializeFast` must round trip.
  Include fixtures that engage the dictionaries (repeated keys, repeated string values), a wide object,
  deep-but-legal nesting, ±`MAX_SAFE_INTEGER`, unicode/emoji, and empty string/object/array.
- Examples: `node examples/commonjs.cjs`, `node examples/esm.mjs`, `npx tsx examples/typescript.ts`,
  `npx tsx examples/language.ts` — all should exit 0.
- Benchmark by **alternating** builds across many short processes (≥7 pairs) and comparing medians.
  A 3-sample comparison on this workload showed a spurious ~11% delta that vanished at 7 pairs.
  A ~140k-node payload gives roughly `deserialize` ~24 ms, `deserializeFast` ~14 ms, `serialize` ~40 ms.
- `npm test` may have **preexisting** failures. Always re-run the suite in the differential worktree and
  compare the failing *set* before blaming the branch. Known preexisting (as of `origin/main` e6ed44b):
  `inspect does not validate payload length` and `BigInt is rejected with the native error`.

## Nesting limit

`MAX_DEPTH = 512` is enforced as `depth >= MAX_DEPTH`, so the effective maximum is **511** nested
levels; 512 throws `BYV nesting too deep`. Serialize paths append
`; a cyclic structure may be the cause` to that message, which is misleading for legitimately deep
acyclic data — don't mistake it for a real cycle-detection failure.

## Devin Secrets Needed

None. Everything builds and runs locally with no credentials, network access or services.
