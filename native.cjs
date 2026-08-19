"use strict";

const load = require("node-gyp-build");

const expectedExports = [
    "version",
    "parse",
    "compile",
    "serialize",
    "serializeFast",
    "deserialize",
    "deserializeFast",
    "inspect"
];

let native;

try {
    native = load(__dirname);
} catch (cause) {
    throw new Error(
        "byv-storage native addon could not be loaded; a prebuilt binary may be unavailable for this platform",
        { cause }
    );
}

const missingExports = expectedExports.filter(
    (name) => typeof native[name] !== "function"
);

if (missingExports.length > 0) {
    throw new Error(
        `byv-storage native addon is missing exports: ${missingExports.join(", ")}`
    );
}

module.exports = native;
