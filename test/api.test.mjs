import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { test } from "node:test";

import byv, {
    compile,
    deserialize,
    deserializeFast,
    inspect,
    parse,
    serialize,
    serializeFast,
    version
} from "../index.js";

const require = createRequire(import.meta.url);
const cjs = require("../index.cjs");

const keys = [
    "version",
    "parse",
    "compile",
    "serialize",
    "serializeFast",
    "deserialize",
    "deserializeFast",
    "inspect"
];

test("the ESM API exposes the complete ordered surface", () => {
    assert.deepEqual(Object.keys(byv), keys);

    for (const key of keys)
        assert.equal(typeof byv[key], "function");

    assert.strictEqual(version, byv.version);
    assert.strictEqual(parse, byv.parse);
    assert.strictEqual(compile, byv.compile);
    assert.strictEqual(serialize, byv.serialize);
    assert.strictEqual(serializeFast, byv.serializeFast);
    assert.strictEqual(deserialize, byv.deserialize);
    assert.strictEqual(deserializeFast, byv.deserializeFast);
    assert.strictEqual(inspect, byv.inspect);
});

test("the CommonJS API is functionally equivalent", () => {
    assert.deepEqual(Object.keys(cjs), keys);

    for (const key of keys)
        assert.equal(typeof cjs[key], "function");

    assert.equal(cjs.version(), byv.version());
    assert.deepEqual(
        cjs.deserialize(
            cjs.serialize({ answer: 42, tags: ["byv", true] })
        ),
        byv.deserialize(
            byv.serialize({ answer: 42, tags: ["byv", true] })
        )
    );
    assert.deepEqual(
        cjs.parse("name -> \"BYV\""),
        byv.parse("name -> \"BYV\"")
    );
    assert.deepEqual(
        cjs.inspect(cjs.serialize({})),
        byv.inspect(byv.serialize({}))
    );
});

test("version is seven and ignores extra arguments", () => {
    assert.equal(byv.version(), 7);
    assert.equal(byv.version("extra", 42), 7);
});

test("serialized values have the BYV v7 header", () => {
    const buffer = byv.serialize({ answer: 42 });

    assert.equal(buffer.subarray(0, 4).toString("ascii"), "BYV7");
    assert.equal(buffer[4], 7);
    assert.equal(buffer[5], 3);
    assert.equal(buffer.readUInt32LE(6), buffer.length - 10);
});
