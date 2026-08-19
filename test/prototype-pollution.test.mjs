import assert from "node:assert/strict";
import test from "node:test";

import byv from "../index.js";

import {
    legacy,
    packed,
    withHeader,
    withPackedHeader
} from "./helpers/binary.mjs";

const decoders = [
    ["deserialize", byv.deserialize],
    ["deserializeFast", byv.deserializeFast]
];

function assertOwnProto(result) {
    assert.equal(
        Object.getPrototypeOf(result),
        Object.prototype);

    const descriptor =
        Object.getOwnPropertyDescriptor(
            result,
            "__proto__");

    assert.deepEqual(descriptor.value, {x: 1});
    assert.equal(descriptor.writable, true);
    assert.equal(descriptor.enumerable, true);
    assert.equal(descriptor.configurable, true);
}

test("legacy payloads cannot replace the prototype of a decoded object", () => {
    const buffer = withHeader(
        0,
        legacy.obj([
            [
                "__proto__",
                legacy.obj([["x", legacy.int(1)]])
            ]
        ]));

    for (const [name, decode] of decoders) {
        const result = decode(buffer);

        assert.equal(typeof result, "object", name);
        assertOwnProto(result);
    }
});

test("packed payloads cannot replace the prototype of a decoded object", () => {
    const buffer = withPackedHeader(
        0x01,
        ["__proto__", "x"],
        [],
        packed.obj([
            [0, packed.obj([[1, packed.int(1)]])]
        ]));

    for (const [name, decode] of decoders) {
        const result = decode(buffer);

        assert.equal(typeof result, "object", name);
        assertOwnProto(result);
    }
});

test("round trips keep __proto__ an own property", () => {
    const value = {["__proto__"]: {x: 1}, keep: true};

    for (const encode of [byv.serialize, byv.serializeFast]) {
        const buffer = encode(value);

        for (const [name, decode] of decoders) {
            const result = decode(buffer);

            assert.equal(result.keep, true, name);
            assertOwnProto(result);
        }
    }
});
