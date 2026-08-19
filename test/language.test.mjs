import assert from "node:assert/strict";
import { test } from "node:test";

import byv from "../index.js";

test("the README language example has equivalent parse and binary paths", () => {
    const source = `
@BYV

status -> "success"
code -> 200

data ::
    id -> 42
    name -> "BYV"

    tags ::
        -"binary"
        -"nodejs"
        -"native"
`;
    const parsed = byv.parse(source);
    const compiled = byv.compile(source);

    assert.deepEqual(parsed, byv.deserialize(compiled));
    assert.deepEqual(parsed, byv.deserializeFast(compiled));
    assert.deepEqual(compiled, byv.serialize(parsed));
});

test("the language parses scalar values", () => {
    assert.deepEqual(
        byv.parse(`
string -> "text"
integer -> -3
float -> 0.5
exponent -> 1e3
yes -> true
no -> false
nothing -> null
`),
        {
            string: "text",
            integer: -3,
            float: 0.5,
            exponent: 1000,
            yes: true,
            no: false,
            nothing: null
        }
    );
});

test("nested and empty blocks support arbitrary indentation", () => {
    assert.deepEqual(
        byv.parse(`
root ::
  child ::
    leaf -> "value"
`),
        {
            root: {
                child: {
                    leaf: "value"
                }
            }
        }
    );
    assert.deepEqual(
        byv.parse(`
empty ::
next -> 1
`),
        {
            empty: {},
            next: 1
        }
    );
    assert.deepEqual(
        byv.parse("root ::\n\tchild ::\n\t\tleaf -> true"),
        {
            root: {
                child: {
                    leaf: true
                }
            }
        }
    );
});

test("the language parses string and object arrays", () => {
    assert.deepEqual(
        byv.parse(`
strings ::
  -"a"
  -"b"
objects ::
  +::
    id -> 1
    name -> "one"
  +::
    id -> 2
    name -> "two"
`),
        {
            strings: ["a", "b"],
            objects: [
                { id: 1, name: "one" },
                { id: 2, name: "two" }
            ]
        }
    );
});

test("keys, headers, whitespace, comments, and duplicate keys work", () => {
    assert.deepEqual(
        byv.parse(`
@BYV

key-with-dash -> 1
key_with_underscore -> 2
duplicate -> 1
duplicate -> 2
# this is a comment
`),
        {
            "key-with-dash": 1,
            key_with_underscore: 2,
            duplicate: 2
        }
    );
    assert.deepEqual(byv.parse(""), {});
    assert.deepEqual(byv.parse("@BYV"), {});
    assert.deepEqual(
        byv.parse("@BYV\r\nname -> \"CRLF\"\r\n"),
        { name: "CRLF" }
    );
});

test("string escapes and unicode are decoded", () => {
    assert.deepEqual(
        byv.parse('@BYV\na -> "line\\nbreak"'),
        { a: "line\nbreak" }
    );
    assert.deepEqual(
        byv.parse('@BYV\na -> "q \\"x\\" end"'),
        { a: 'q "x" end' }
    );
    assert.deepEqual(
        byv.parse('@BYV\na -> "tab\\there"'),
        { a: "tab\there" }
    );
    assert.deepEqual(
        byv.parse('@BYV\na -> "back\\\\slash"'),
        { a: "back\\slash" }
    );
});

test("language parser errors include their source lines", () => {
    const cases = [
        [
            "@BYV\nname ->",
            "BYV parser: expected value at line 2"
        ],
        [
            "@BYV\n!!!???",
            "BYV lexer error at line 2: Unexpected character"
        ],
        [
            "@BYV\n// comment\nname -> \"x\"",
            "BYV lexer error at line 2: Unexpected character"
        ],
        [
            "@BYV\na = 1",
            "BYV lexer error at line 2: Unexpected character"
        ],
        [
            "@BYV\na->1",
            "BYV lexer error at line 2: Unexpected character"
        ],
        [
            "@FOO\na -> 1",
            "BYV lexer error at line 1: Unexpected character"
        ],
        [
            "@BYV\na -> \"x",
            "BYV lexer error at line 2: Unterminated string"
        ],
        [
            '@BYV\n"a b" -> 1',
            "BYV parser: expected identifier at line 2"
        ],
        [
            "@BYV\na -> 1 2",
            "BYV parser: expected identifier at line 2"
        ],
        [
            "@BYV\n-\"item\"",
            "BYV parser: expected identifier at line 2"
        ]
    ];

    for (const [source, message] of cases)
        assert.throws(() => byv.parse(source), { message });
});

test("only quoted string and object array items are supported", () => {
    // Non-string scalar list items are not supported by the language grammar.
    for (const item of ["-1", "-1.5"]) {
        assert.throws(
            () => byv.parse(`@BYV\nl ::\n    ${item}`),
            { message: "BYV parser: expected identifier at line 3" }
        );
    }

    for (const item of ["- 1", "-true", "-false", "-null"]) {
        assert.throws(
            () => byv.parse(`@BYV\nl ::\n    ${item}`),
            { message: "BYV lexer error at line 3: Unexpected character" }
        );
    }
});

test("parse and compile validate their required arguments", () => {
    assert.throws(
        () => byv.parse(),
        { message: "parse() requires BYV string" }
    );
    assert.throws(
        () => byv.parse(42),
        { message: "parse() requires BYV string" }
    );
    assert.throws(
        () => byv.compile(),
        { message: "compile() requires BYV source" }
    );
});
