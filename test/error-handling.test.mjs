import test from "node:test";
import assert from "node:assert/strict";
import byv from "../index.js";

const decoders = [
    ["deserialize", byv.deserialize],
    ["deserializeFast", byv.deserializeFast]
];

function binary(payload, flags = 0, version = 7) {
    const result = Buffer.alloc(10 + payload.length);
    result.write("BYV7", 0, "ascii");
    result[4] = version;
    result[5] = flags;
    result.writeUInt32LE(payload.length, 6);
    payload.copy(result, 10);
    return result;
}

function expectError(fn, message, ErrorType = Error, label = "call") {
    assert.throws(fn, (error) => {
        assert.ok(
            error instanceof ErrorType,
            `${label} threw ${error} instead of ${ErrorType.name}`
        );
        if (message)
            assert.match(error.message, message, `from ${label}`);
        return true;
    });
}

function expectDecodeError(buffer, message) {
    for (const [name, decode] of decoders)
        expectError(() => decode(buffer), message, Error, name);
}

function packed(payload, flags = 1) {
    return binary(payload, flags);
}

function deeplyNestedBuffer(depth) {
    const payload = Buffer.alloc(depth * 5 + 2);
    let offset = 0;
    for (let i = 0; i < depth; i++) {
        payload[offset++] = 6;
        payload.writeUInt32LE(1, offset);
        offset += 4;
    }
    payload[offset] = 0;
    return binary(payload);
}

function deeplyNestedSource(depth) {
    const lines = ["@BYV"];
    for (let i = 0; i < depth; i++)
        lines.push(`${" ".repeat(i * 4)}level${i} ::`);
    return lines.join("\n");
}

test("valid data round-trips through both serializers and deserializers", () => {
    const value = {
        name: "BYV",
        active: true,
        nested: {
            count: 7,
            tags: ["binary", "nodejs", "binary"],
            empty: null
        }
    };

    const first = byv.serialize(value);
    assert.deepEqual(byv.deserialize(first), value);
    assert.deepEqual(byv.deserializeFast(first), value);

    const second = byv.serializeFast(byv.deserialize(first));
    assert.deepEqual(byv.deserialize(second), value);
    assert.deepEqual(byv.deserializeFast(second), value);
});

test("parse and compile round-trip the README sample", () => {
    const source = `@BYV

name -> "BYV"
version -> 7
active -> true

tags ::
    -"binary"
    -"nodejs"
    -"🚀"`;

    const expected = {
        name: "BYV",
        version: 7,
        active: true,
        tags: ["binary", "nodejs", "🚀"]
    };

    assert.deepEqual(byv.parse(source), expected);
    assert.deepEqual(byv.deserialize(byv.compile(source)), expected);
});

test("rejects malformed headers and trailing data on both decode paths", () => {
    const valid = byv.serialize({ value: 1 });

    expectDecodeError(valid.subarray(0, valid.length - 1), /payload size|truncated/i);

    const badMagic = Buffer.from(valid);
    badMagic[0] = "X".charCodeAt(0);
    expectDecodeError(badMagic, /magic/i);

    const badVersion = Buffer.from(valid);
    badVersion[4] = 6;
    expectDecodeError(badVersion, /version/i);

    const badPayload = Buffer.from(valid);
    badPayload.writeUInt32LE(badPayload.length - 9, 6);
    expectDecodeError(badPayload, /payload size/i);

    const trailing = Buffer.concat([valid, Buffer.from([0])]);
    trailing.writeUInt32LE(trailing.length - 10, 6);
    expectDecodeError(trailing, /trailing data/i);
});

test("rejects invalid flags, booleans, and type bytes", () => {
    const valid = byv.serialize({ value: true });

    const unknownFlags = Buffer.from(valid);
    unknownFlags[5] = 0x04;
    expectDecodeError(unknownFlags, /unknown flags/i);

    const inconsistentFlags = Buffer.from(valid);
    inconsistentFlags[5] = 0x02;
    expectDecodeError(inconsistentFlags, /requires packed keys/i);

    const invalidBoolean = binary(Buffer.from([1, 2]));
    expectDecodeError(invalidBoolean, /boolean/i);

    expectDecodeError(binary(Buffer.from([0xff])), /unknown.*type/i);
});

test("rejects invalid UTF-8 in raw strings and dictionary keys", () => {
    const invalidString = binary(
        Buffer.from([4, 2, 0, 0, 0, 0xc0, 0x80])
    );
    expectDecodeError(invalidString, /UTF-8/i);

    const invalidKey = packed(
        Buffer.from([1, 1, 0, 0, 0, 0xff, 0])
    );
    expectDecodeError(invalidKey, /UTF-8/i);
});

test("rejects packed dictionary ids outside their ranges", () => {
    const invalidKeyId = packed(
        Buffer.from([1, 0, 0, 0, 0, 5, 1, 0, 0, 0, 5, 0])
    );
    expectDecodeError(invalidKeyId, /key id out of range/i);

    const invalidStringId = packed(
        Buffer.from([0, 0, 4, 1, 1])
    , 3);
    expectDecodeError(invalidStringId, /string id out of range/i);
});

test("rejects overflowing VarUInt encodings", () => {
    const overflow = packed(
        Buffer.from([0x80, 0x80, 0x80, 0x80, 0x80,
            0x80, 0x80, 0x80, 0x80, 0x02])
    );
    expectDecodeError(overflow, /varuint overflow/i);
});

test("rejects deeply nested binary and source input", () => {
    expectDecodeError(deeplyNestedBuffer(513), /nesting too deep/i);

    const source = deeplyNestedSource(513);
    expectError(() => byv.parse(source), /nesting too deep/i);
    expectError(() => byv.compile(source), /nesting too deep/i);
});

test("rejects cyclic JavaScript values without crashing", () => {
    const cyclic = {};
    cyclic.self = cyclic;

    expectError(
        () => byv.serialize(cyclic),
        /nesting too deep|cyclic/i
    );
    expectError(
        () => byv.serializeFast(cyclic),
        /nesting too deep|cyclic/i
    );
});

test("reports source integer overflow with line and literal", () => {
    const source = "@BYV\nvalue -> 999999999999999999999999999999";
    for (const parse of [byv.parse, byv.compile]) {
        expectError(
            () => parse(source),
            /line 2.*integer literal out of range.*999999999999999999999999999999/i
        );
    }
});

test("preserves an exception thrown by a JavaScript getter", () => {
    const original = new TypeError("getter failed");
    const input = {};
    Object.defineProperty(input, "value", {
        enumerable: true,
        get() {
            throw original;
        }
    });

    for (const serialize of [byv.serialize, byv.serializeFast]) {
        assert.throws(
            () => serialize(input),
            (error) => error === original
        );
    }
});

test("all argument validation failures are TypeErrors", () => {
    const calls = [
        () => byv.parse(),
        () => byv.compile(1),
        () => byv.serialize(),
        () => byv.serializeFast(),
        () => byv.deserialize("buffer"),
        () => byv.deserializeFast("buffer"),
        () => byv.inspect("buffer")
    ];

    for (const call of calls)
        expectError(call, null, TypeError);
});

test("inspect only accepts structurally valid headers as valid", () => {
    const valid = byv.serialize({ value: 1 });
    const inspected = byv.inspect(valid);
    assert.equal(inspected.valid, true);
    assert.equal(inspected.version, 7);
    assert.equal(inspected.byteLength, valid.length);
    assert.equal(inspected.payloadSize, valid.length - 10);

    const invalid = Buffer.from(valid);
    invalid[4] = 6;
    assert.equal(byv.inspect(invalid).valid, false);

    invalid[4] = 7;
    invalid.writeUInt32LE(invalid.length - 9, 6);
    assert.equal(byv.inspect(invalid).valid, false);
});
