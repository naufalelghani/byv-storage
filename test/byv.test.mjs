import assert from "node:assert/strict";
import test from "node:test";

import byv from "../index.js";

const HEADER_SIZE = 10;

function legacyBuffer(payload) {
    const head = Buffer.alloc(HEADER_SIZE);

    head.write("BYV7", 0, "ascii");
    head[4] = byv.version();
    head[5] = 0;
    head.writeUInt32LE(payload.length, 6);

    return Buffer.concat([head, Buffer.from(payload)]);
}

function nest(depth) {
    let value = 0;

    for (let i = 0; i < depth; i++)
        value = [value];

    return value;
}

const sample = {
    name: "BYV",
    version: 7,
    active: true,
    missing: null,
    ratio: 1.5,
    tags: [
        "binary",
        "nodejs",
        "🚀",
        "binary"
    ],
    nested: {
        list: [
            {
                id: 1,
                name: "Alice"
            },
            {
                id: 2,
                name: "Bob"
            }
        ]
    }
};

test("serialize/deserialize round trip", () => {
    assert.deepEqual(
        byv.deserialize(byv.serialize(sample)),
        sample);

    assert.deepEqual(
        byv.deserializeFast(byv.serializeFast(sample)),
        sample);

    assert.deepEqual(
        byv.deserializeFast(byv.serialize(sample)),
        sample);

    assert.deepEqual(
        byv.deserialize(byv.serializeFast(sample)),
        sample);
});

test("legacy payloads without dictionaries still decode", () => {
    const buffer = legacyBuffer([
        6, 2, 0, 0, 0, // array, 2 items
        1, 1, // true
        0 // null
    ]);

    assert.deepEqual(byv.deserialize(buffer), [true, null]);
    assert.deepEqual(byv.deserializeFast(buffer), [true, null]);
});

test("compile/parse of BYV language sources", () => {
    const source = [
        "@BYV",
        "",
        "name -> \"BYV\"",
        "meta ::",
        "    version -> 7",
        "    tags ::",
        "        -\"a\"",
        "        -\"b\""
    ].join("\n");

    const expected = {
        name: "BYV",
        meta: {
            version: 7,
            tags: ["a", "b"]
        }
    };

    assert.deepEqual(byv.parse(source), expected);
    assert.deepEqual(byv.deserialize(byv.compile(source)), expected);
});

test("nesting just below the limit is accepted", () => {
    const value = nest(500);

    assert.deepEqual(byv.deserialize(byv.serialize(value)), value);
    assert.deepEqual(byv.deserializeFast(byv.serializeFast(value)), value);
});

test("deeply nested values are rejected instead of crashing", () => {
    const value = nest(5000);

    assert.throws(() => byv.serialize(value), /nesting too deep/);
    assert.throws(() => byv.serializeFast(value), /nesting too deep/);

    // ~200k nesting levels, five bytes each.
    const payload = [];

    for (let i = 0; i < 200000; i++)
        payload.push(6, 1, 0, 0, 0);

    payload.push(0);

    const buffer = legacyBuffer(payload);

    assert.throws(() => byv.deserialize(buffer), /nesting too deep/);
    assert.throws(() => byv.deserializeFast(buffer), /nesting too deep/);
});

test("declared item counts must fit in the remaining bytes", () => {
    const payload = [];

    // Each level claims 1,000,000 children in five bytes.
    for (let i = 0; i < 400; i++)
        payload.push(6, 0x40, 0x42, 0x0f, 0x00);

    payload.push(0);

    const buffer = legacyBuffer(payload);

    assert.throws(() => byv.deserialize(buffer), /truncated/);
    assert.throws(() => byv.deserializeFast(buffer), /truncated/);
});

test("unsupported flag combinations are rejected", () => {
    const buffer = byv.serialize(sample);

    for (const flags of [0x02, 0x04, 0xff]) {
        const hostile = Buffer.from(buffer);
        hostile[5] = flags;

        assert.throws(() => byv.deserialize(hostile), /flags/);
        assert.throws(() => byv.deserializeFast(hostile), /flags/);
    }
});

test("decoded keys cannot replace the prototype of the result", () => {
    const key = Buffer.from("__proto__", "utf8");

    const buffer = legacyBuffer([
        5, 1, 0, 0, 0, // object, 1 entry
        key.length, 0, 0, 0, ...key,
        5, 1, 0, 0, 0, // value: object, 1 entry
        1, 0, 0, 0, 0x78, // key "x"
        2, 1, 0, 0, 0, 0, 0, 0, 0 // int 1
    ]);

    for (const decode of [byv.deserialize, byv.deserializeFast]) {
        const result = decode(buffer);

        assert.equal(Object.getPrototypeOf(result), Object.prototype);
        assert.deepEqual(
            Object.getOwnPropertyDescriptor(result, "__proto__").value,
            {x: 1});
    }
});
