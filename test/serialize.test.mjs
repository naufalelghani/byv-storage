import assert from "node:assert/strict";
import { test } from "node:test";

import byv from "../index.js";

const deeplyNested = {
    root: [
        {
            child: {
                values: [1, "two", false, null]
            }
        },
        {
            sibling: {
                answer: 42
            }
        }
    ]
};

let fiftyLevels = null;

for (let index = 49; index >= 0; index--)
    fiftyLevels = { level: index, next: fiftyLevels };

const threeHundredKeys = Object.fromEntries(
    Array.from(
        { length: 300 },
        (_, index) => [`key${index}`, index]
    )
);

const tenThousandNumbers = Array.from(
    { length: 10000 },
    (_, index) => index
);

const values = [
    {},
    [],
    "hello",
    "",
    "🚀é中",
    42,
    -7,
    0,
    4.85,
    -1.5,
    2 ** 53,
    Number.MAX_SAFE_INTEGER,
    Number.MIN_VALUE,
    0.1 + 0.2,
    true,
    false,
    null,
    deeplyNested,
    [1, "a", true, null, { x: 1 }],
    "x".repeat(1000),
    threeHundredKeys,
    fiftyLevels,
    [[1, [2, [3]]]],
    tenThousandNumbers
];

const combinations = [
    ["serialize + deserialize", byv.serialize, byv.deserialize],
    ["serializeFast + deserializeFast", byv.serializeFast, byv.deserializeFast],
    ["serialize + deserializeFast", byv.serialize, byv.deserializeFast],
    ["serializeFast + deserialize", byv.serializeFast, byv.deserialize]
];

test("all encoder and decoder combinations roundtrip representative values", () => {
    for (const [name, encode, decode] of combinations) {
        for (const value of values) {
            const result = decode(encode(value));

            assert.deepEqual(
                result,
                value,
                `${name} failed for ${typeof value}`
            );
        }
    }

    const decodedArray = byv.deserialize(
        byv.serialize(tenThousandNumbers)
    );

    assert.equal(decodedArray.length, 10000);
    assert.equal(decodedArray[0], 0);
    assert.equal(decodedArray[4999], 4999);
    assert.equal(decodedArray[9999], 9999);
});

test("serialize and serializeFast produce identical bytes", () => {
    assert.deepEqual(
        byv.serialize({ a: 1, b: "x" }),
        byv.serializeFast({ a: 1, b: "x" })
    );
    assert.deepEqual(
        byv.serialize([1, "two", true, null]),
        byv.serializeFast([1, "two", true, null])
    );
});

test("string dictionaries roundtrip repeated strings through all paths", () => {
    const values = [
        [
            { k: "dup" },
            { k: "dup" },
            { k: "dup" }
        ],
        {
            a: "same",
            b: "same",
            c: "same",
            d: "other"
        }
    ];

    for (const value of values) {
        for (const [, encode, decode] of combinations)
            assert.deepEqual(decode(encode(value)), value);
    }
});

test("object key insertion order survives roundtrip", () => {
    const value = { z: 1, a: 2, m: 3 };
    const result = byv.deserialize(byv.serialize(value));

    assert.deepEqual(Object.keys(result), ["z", "a", "m"]);
});

test("non-finite numbers and negative zero preserve observed behavior", () => {
    const decoders = [
        byv.deserialize,
        byv.deserializeFast
    ];

    for (const decode of decoders) {
        assert.equal(
            Number.isNaN(decode(byv.serialize(NaN))),
            true
        );
        assert.equal(
            decode(byv.serialize(Infinity)),
            Infinity
        );
        assert.equal(
            decode(byv.serialize(-Infinity)),
            -Infinity
        );

        const negativeZero = decode(byv.serialize(-0));

        // Integer encoding does not preserve the negative-zero sign.
        assert.equal(negativeZero, 0);
        assert.equal(Object.is(negativeZero, -0), false);
    }
});

test("undefined values become null", () => {
    const topLevel = byv.serialize(undefined);

    assert.equal(topLevel.length, 13);
    assert.equal(byv.deserialize(topLevel), null);
    assert.deepEqual(
        byv.deserialize(
            byv.serialize({ a: undefined, b: 1 })
        ),
        { a: null, b: 1 }
    );
});

test("serialization handles enumerable JavaScript properties", () => {
    const symbol = Symbol("hidden");
    const object = { a: 1, [symbol]: 2 };

    class Example {
        constructor() {
            this.a = 1;
        }
    }

    assert.deepEqual(
        byv.deserialize(byv.serialize(object)),
        { a: 1 }
    );
    assert.deepEqual(
        byv.deserialize(byv.serialize(new Example())),
        { a: 1 }
    );
    assert.deepEqual(
        byv.deserialize(byv.serialize([1, , 3])),
        [1, null, 3]
    );
    assert.deepEqual(
        byv.deserialize(byv.serialize(new Date(0))),
        {}
    );
    assert.deepEqual(
        byv.deserialize(byv.serialize(() => 42)),
        {}
    );
});

test("BigInt is rejected with the native error", () => {
    assert.throws(
        () => byv.serialize(1n),
        (error) => {
            assert.equal(error.constructor, Error);
            assert.equal(error.name, "Error");
            assert.equal(error.message, "Unsupported JavaScript value");
            return true;
        }
    );
});

test("serializers validate their required argument", () => {
    assert.throws(
        () => byv.serialize(),
        {
            name: "TypeError",
            message: "serialize() requires argument"
        }
    );
    assert.throws(
        () => byv.serializeFast(),
        {
            name: "TypeError",
            message: "serializeFast() requires argument"
        }
    );
});

test("serializers ignore extra arguments", () => {
    assert.deepEqual(
        byv.deserialize(
            byv.serialize({ a: 1 }, "extra")
        ),
        { a: 1 }
    );
    assert.deepEqual(
        byv.deserialize(
            byv.serializeFast({ a: 1 }, "extra")
        ),
        { a: 1 }
    );
});
