import byv from "../index.js";

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
    decoded,
    { depth: null }
);