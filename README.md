# byv-storage

High-performance binary serialization format and runtime for Node.js, powered by a native C++ core.

BYV provides:

- BYV v7 binary serialization
- Fast native serialization and deserialization
- Packed dictionary-based encoding
- Optional string dictionary compression
- BYV Language (`.byv.lx`) parser and compiler
- Native Node.js integration through Node-API
- CommonJS, ESM, and TypeScript support

## Installation

```bash
npm install byv-storage
```

> Native prebuilt binaries for supported platforms will be distributed with the package. When a prebuilt binary is unavailable for a platform, a local native build may be required.

## Quick Start

### CommonJS

```js
const byv = require("byv-storage");

const data = {
    name: "BYV",
    version: 7,
    active: true,
    tags: [
        "binary",
        "nodejs",
        "🚀"
    ]
};

const encoded = byv.serializeFast(data);

console.log("BYV bytes:", encoded.length);

const decoded = byv.deserializeFast(encoded);

console.dir(decoded, {
    depth: null
});
```

### ESM

```js
import byv from "byv-storage";

const data = {
    name: "BYV",
    version: 7,
    active: true,
    tags: [
        "binary",
        "nodejs",
        "🚀"
    ]
};

const encoded = byv.serializeFast(data);

console.log("BYV bytes:", encoded.length);

const decoded = byv.deserializeFast(encoded);

console.dir(decoded, {
    depth: null
});
```

### TypeScript

```ts
import byv from "byv-storage";

const data = {
    name: "BYV",
    version: 7,
    active: true,
    tags: [
        "binary",
        "typescript",
        "🚀"
    ]
};

const encoded = byv.serializeFast(data);

const decoded = byv.deserializeFast(encoded);

console.dir(decoded, {
    depth: null
});
```

## API

### `byv.version()`

Returns the BYV format version supported by the native core.

```js
console.log(byv.version());
```

Current version:

```text
7
```

### `byv.serialize(value)`

Serializes a JavaScript value into BYV binary.

```js
const buffer = byv.serialize({
    name: "BYV",
    active: true
});
```

### `byv.serializeFast(value)`

Optimized native serialization path.

```js
const buffer = byv.serializeFast({
    name: "BYV",
    active: true
});
```

### `byv.deserialize(buffer)`

Decodes BYV binary using the normal decoder.

```js
const value = byv.deserialize(buffer);
```

### `byv.deserializeFast(buffer)`

Decodes BYV binary using the optimized native decoder.

```js
const value = byv.deserializeFast(buffer);
```

### `byv.parse(source)`

Parses BYV Language source into a JavaScript-compatible value.

```js
const source = `
@BYV

name -> "BYV"
version -> 7
active -> true
`;

const value = byv.parse(source);

console.dir(value, {
    depth: null
});
```

### `byv.compile(source)`

Compiles BYV Language directly into BYV v7 binary.

```js
const source = `
@BYV

name -> "BYV"
version -> 7
active -> true

tags ::
    -"binary"
    -"nodejs"
    -"🚀"
`;

const binary = byv.compile(source);

console.log(
    "BYV bytes:",
    binary.length
);
```

### `byv.inspect(buffer)`

Returns basic information about a BYV binary buffer.

```js
console.dir(
    byv.inspect(buffer),
    { depth: null }
);
```

The returned `valid` field is `true` only when the buffer has the `BYV7`
magic, version 7, a payload size matching the bytes after the 10-byte header,
and valid flags (only bits `0x01` and `0x02`, with `0x02` requiring `0x01`).
The `version`, `byteLength`, `payloadSize`, and `flags` fields retain their
existing shapes; `payloadSize` and `flags` are included only when the buffer
is at least 10 bytes long.

## BYV Language

BYV also includes a compact structured language designed for human-readable data definitions.

Example:

```text
@BYV

status -> "success"
code -> 200
message -> "Data successfully retrieved"

data ::
    id -> 42
    title -> "Advanced BYV Data Structures"

    author ::
        id -> 7
        name -> "Alice Smith"
        email -> "alice.smith@example.com"
        verified -> true

    tags ::
        -"json"
        -"binary"
        -"backend"
        -"BYV"

    metrics ::
        views -> 15420
        likes -> 1240
        rating -> 4.85
```

The typical flow is:

```text
BYV Language source
        ↓
      parse()
        ↓
   JavaScript value
        ↓
     compile()
        ↓
     BYV v7 binary
        ↓
 deserialize()
        ↓
     JavaScript value
```

The same binary can also be decoded using `deserializeFast()`.

## End-to-End Example

```js
const source = `
@BYV

status -> "success"
code -> 200

data ::
    id -> 42
    name -> "BYV"

    tags ::
        -"binary"
        -"nodejs"
        -"native"
`;

const parsed = byv.parse(source);

const binary = byv.compile(source);

const normal = byv.deserialize(binary);

const fast = byv.deserializeFast(binary);

console.log(
    JSON.stringify(parsed) ===
    JSON.stringify(normal)
);

console.log(
    JSON.stringify(parsed) ===
    JSON.stringify(fast)
);
```

Both comparisons should return:

```text
true
true
```

## Packed Encoding

BYV v7 supports packed dictionary encoding.

Packed encoding reduces repeated object-key overhead by storing keys in a dictionary and referencing them using compact VarUInt identifiers.

The native implementation also supports an optional string dictionary for repeated string values.

Conceptually:

```text
BYV v7
│
├── Direct format
│
└── Packed format
    ├── Key dictionary
    └── Optional string dictionary
```

The native serializer uses the optimized packed representation internally.

## Binary Format

BYV v7 uses a compact binary header:

```text
4 bytes   Magic
1 byte    Version
1 byte    Flags
4 bytes   Payload size
```

The current magic value is:

```text
BYV7
```

Supported primitive/container types include:

```text
Null
Bool
Int
Float
String
Object
Array
```

## Validation and Safety

The decoder validates malformed binary input, including:

- invalid magic
- unsupported versions
- payload size mismatches
- invalid boolean values
- unknown type identifiers
- truncated buffers
- invalid UTF-8
- dictionary IDs outside the valid range
- string dictionary IDs outside the valid range
- VarUInt overflow
- oversized collections
- unknown or inconsistent flags
- trailing data after the root value
- excessive nesting depth

Invalid boolean values are restricted to:

```text
0 → false
1 → true
```

Other values are rejected.

## Testing

The project is tested against:

- golden vectors
- negative/malformed vectors
- exhaustive roundtrip cases
- resource-limit cases
- fuzzed binary input
- version compatibility
- conformance matrix
- direct interoperability
- packed interoperability

The C++ and Rust implementations are tested against the same BYV v7 binary contract.

Current interoperability coverage includes:

```text
C++ → Rust
Rust → C++
```

for both direct and packed representations.

## Native Runtime

`byv-storage` uses a native C++ implementation through Node-API.

The native core exposes:

```text
version
parse
compile
serialize
serializeFast
deserialize
deserializeFast
inspect
```

This provides native binary processing while keeping the public JavaScript API simple.

## Examples

The package includes examples for:

```text
examples/
├── commonjs.cjs
├── esm.mjs
├── typescript.ts
└── language.ts
```

The `language.ts` example demonstrates:

```text
BYV Language
    ↓
parse()
    ↓
compile()
    ↓
BYV binary
    ↓
deserialize()
    ↓
deserializeFast()
```

## Current Scope

BYV is currently focused on:

- binary serialization
- binary deserialization
- structured data
- packed storage
- BYV Language
- native Node.js performance
- cross-implementation compatibility

Partial in-place CRUD/update operations are not currently part of the public storage API.

## License

MIT License. See [LICENSE](LICENSE).

Copyright (c) 2026 Byval - Naufal Elghani