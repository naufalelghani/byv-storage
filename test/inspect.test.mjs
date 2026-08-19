import assert from "node:assert/strict";
import { test } from "node:test";

import byv from "../index.js";

test("inspect reports valid serialized and compiled buffers", () => {
    const serialized = byv.serialize({ a: 1 });
    const compiled = byv.compile("a -> 1");

    for (const buffer of [serialized, compiled]) {
        assert.deepEqual(
            byv.inspect(buffer),
            {
                valid: true,
                version: 7,
                byteLength: buffer.length,
                payloadSize: buffer.length - 10,
                flags: 3
            }
        );
    }
});

test("inspect reports invalid magic for complete headers", () => {
    assert.deepEqual(
        byv.inspect(Buffer.alloc(12)),
        {
            valid: false,
            version: 0,
            byteLength: 12,
            payloadSize: 0,
            flags: 0
        }
    );
});

test("inspect omits payload fields for short buffers", () => {
    for (const buffer of [Buffer.alloc(4), Buffer.alloc(0)]) {
        const result = byv.inspect(buffer);

        assert.equal(result.valid, false);
        assert.equal(result.version, 0);
        assert.equal(result.byteLength, buffer.length);
        assert.equal(Object.hasOwn(result, "payloadSize"), false);
        assert.equal(Object.hasOwn(result, "flags"), false);
        assert.deepEqual(
            Object.keys(result),
            ["valid", "version", "byteLength"]
        );
    }
});

test("inspect rejects payload length mismatches", () => {
    const buffer = byv.serialize({});
    const truncated = buffer.subarray(0, 13);

    assert.deepEqual(
        byv.inspect(truncated),
        {
            valid: false,
            version: 7,
            byteLength: 13,
            payloadSize: 7,
            flags: 3
        }
    );
});

test("inspect validates its required argument", () => {
    assert.throws(
        () => byv.inspect(),
        { message: "inspect() requires Buffer" }
    );
    assert.throws(
        () => byv.inspect("not a buffer"),
        { message: "inspect() requires Buffer" }
    );
});
