import assert from "node:assert/strict";
import { test } from "node:test";

import byv from "../index.js";
import {
    legacy,
    packed,
    varUInt,
    withHeader,
    withPackedHeader
} from "./helpers/binary.mjs";

const decoders = [
    ["deserialize", byv.deserialize],
    ["deserializeFast", byv.deserializeFast]
];

test("decoders validate their Buffer argument", () => {
    for (const [name, decode] of decoders) {
        const message = `${name}() requires Buffer`;

        assert.throws(() => decode(), { message });
        assert.throws(() => decode("not a buffer"), { message });
        assert.throws(() => decode([]), { message });
        assert.throws(() => decode(new ArrayBuffer(0)), { message });
    }
});

test("Uint8Array views over BYV buffers are accepted", () => {
    const buffer = byv.serialize({ a: 1 });
    const view = new Uint8Array(
        buffer.buffer,
        buffer.byteOffset,
        buffer.byteLength
    );

    for (const [, decode] of decoders)
        assert.deepEqual(decode(view), { a: 1 });
});

test("decoders reject invalid headers identically", () => {
    const valid = byv.serialize({ a: 1 });
    const cases = [
        [
            "short buffer",
            Buffer.alloc(0),
            "Invalid BYV buffer"
        ],
        [
            "short header",
            Buffer.alloc(9),
            "Invalid BYV buffer"
        ],
        [
            "wrong magic",
            (() => {
                const buffer = Buffer.from(valid);
                buffer.write("BYV6", 0, "ascii");
                return buffer;
            })(),
            "Invalid BYV magic"
        ],
        [
            "wrong version",
            (() => {
                const buffer = Buffer.from(valid);
                buffer[4] = 6;
                return buffer;
            })(),
            "Unsupported BYV version"
        ],
        [
            "truncated payload",
            valid.subarray(0, valid.length - 2),
            "BYV payload size mismatch"
        ],
        [
            "wrong declared payload size",
            (() => {
                const buffer = Buffer.from(valid);
                buffer.writeUInt32LE(buffer.length - 9, 6);
                return buffer;
            })(),
            "BYV payload size mismatch"
        ],
        [
            "oversized packed dictionary",
            withHeader(
                1,
                Buffer.from([0xff, 0xff, 0xff, 0xff, 0x0f])
            ),
            "BYV dictionary too large"
        ]
    ];

    for (const [label, buffer, message] of cases) {
        for (const [name, decode] of decoders) {
            assert.throws(
                () => decode(buffer),
                { message },
                `${name}: ${label}`
            );
        }
    }
});

test("both decoders read legacy v7 values", () => {
    const cases = [
        [legacy.int(42), 42],
        [legacy.null(), null],
        [legacy.bool(true), true],
        [legacy.bool(false), false],
        [legacy.float(1.5), 1.5],
        [legacy.str("hi"), "hi"],
        [
            legacy.obj({
                a: legacy.int(7)
            }),
            { a: 7 }
        ],
        [
            legacy.arr([
                legacy.int(1),
                legacy.int(2)
            ]),
            [1, 2]
        ]
    ];

    for (const [payload, expected] of cases) {
        const buffer = withHeader(0, payload);

        for (const [, decode] of decoders)
            assert.deepEqual(decode(buffer), expected);
    }
});

test("both decoders read packed values", () => {
    const cases = [
        [
            withPackedHeader(
                1,
                ["a"],
                [],
                packed.obj([
                    [0, packed.int(9)]
                ])
            ),
            { a: 9 }
        ],
        [
            withPackedHeader(
                3,
                ["k"],
                ["v"],
                packed.obj([
                    [0, packed.strRef(0)]
                ])
            ),
            { k: "v" }
        ],
        [
            withPackedHeader(
                3,
                ["k"],
                ["v"],
                packed.obj([
                    [0, packed.str("raw", true)]
                ])
            ),
            { k: "raw" }
        ]
    ];

    for (const [buffer, expected] of cases) {
        for (const [, decode] of decoders)
            assert.deepEqual(decode(buffer), expected);
    }
});

test("packed decoders reject invalid dictionary references and markers", () => {
    const cases = [
        [
            withPackedHeader(
                1,
                ["a"],
                [],
                packed.obj([
                    [5, packed.int(9)]
                ])
            ),
            "BYV packed key id out of range"
        ],
        [
            withPackedHeader(
                3,
                ["k"],
                ["v"],
                packed.obj([
                    [0, packed.strRef(7)]
                ])
            ),
            "BYV packed string id out of range"
        ],
        [
            withPackedHeader(
                3,
                ["k"],
                ["v"],
                packed.obj([
                    [0, packed.strMarker(9)]
                ])
            ),
            "BYV invalid string marker"
        ]
    ];

    for (const [buffer, message] of cases) {
        for (const [name, decode] of decoders) {
            assert.throws(
                () => decode(buffer),
                { message },
                `${name}: ${message}`
            );
        }
    }
});

test("packed decoders enforce collection and dictionary limits", () => {
    const cases = [
        [
            withPackedHeader(
                1,
                [],
                [],
                packed.objCount(2000000)
            ),
            "BYV object too large"
        ],
        [
            withPackedHeader(
                1,
                [],
                [],
                packed.arrCount(2000000)
            ),
            "BYV array too large"
        ],
        [
            withHeader(
                3,
                Buffer.concat([
                    varUInt(0),
                    Buffer.from([
                        0xff,
                        0xff,
                        0xff,
                        0xff,
                        0x0f
                    ])
                ])
            ),
            "BYV string dictionary too large"
        ],
        [
            withHeader(
                1,
                Buffer.alloc(10, 0x80)
            ),
            "BYV varuint overflow"
        ]
    ];

    for (const [buffer, message] of cases) {
        for (const [name, decode] of decoders) {
            assert.throws(
                () => decode(buffer),
                { message },
                `${name}: ${message}`
            );
        }
    }
});

test("legacy decoders reject malformed values", () => {
    const cases = [
        [
            withHeader(0, Buffer.from([1, 2])),
            "BYV invalid boolean value"
        ],
        [
            withHeader(0, Buffer.from([9])),
            "Unknown BYV type"
        ],
        [
            withHeader(0, Buffer.from([2, 1, 2])),
            "BYV binary buffer truncated"
        ]
    ];

    for (const [buffer, message] of cases) {
        for (const [, decode] of decoders)
            assert.throws(() => decode(buffer), { message });
    }
});
