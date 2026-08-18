"use strict";

const byv = require(
    "../index.cjs"
);

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

const encoded =
    byv.serializeFast(data);

console.log(
    "BYV bytes:",
    encoded.length
);

const decoded =
    byv.deserializeFast(
        encoded
    );

console.dir(
    decoded, { depth: null }
);