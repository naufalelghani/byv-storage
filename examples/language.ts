import { createRequire } from "node:module";

const require =
    createRequire(
        import.meta.url
    );

const byv =
    require("../index.cjs");

const source = `
@BYV

status -> "success"
code -> 200
message -> "Data successfully retrieved"

data ::
    id -> 42
    title -> "Advanced BYV Data Structures"
    slug -> "advanced-byv-data-structures"

    author ::
        id -> 7
        name -> "Alice Smith"
        email -> "alice.smith@example.com"
        verified -> true

        socials ::
            twitter -> "@alicesmith"
            github -> "alicesmith-dev"

    category ::
        id -> 3
        name -> "Programming"
        parentId -> null

    tags ::
        -"json"
        -"tutorial"
        -"beginner"
        -"api"
        -"backend"

    metrics ::
        views -> 15420
        likes -> 1240
        rating -> 4.85
        shares -> 312

meta ::
    requestId -> "abc-123-xyz-789"
    duration -> 12
    server -> "api-node-04"

pagination ::
    currentPage -> 1
    totalPages -> 5
    perPage -> 10
    totalItems -> 48
`;

console.log(
    "======================================================================"
);

console.log(
    "             BYV LANGUAGE → BINARY → JS END-TO-END"
);

console.log(
    "======================================================================"
);

console.log(
    "\nBYV version:",
    byv.version()
);

// ------------------------------------------------------------
// PARSE
// ------------------------------------------------------------

console.log(
    "\n[1] PARSE BYV LANGUAGE"
);

const parsed =
    byv.parse(
        source
    );

console.dir(
    parsed,
    { depth: null }
);

console.log(
    "✓ PARSE PASS"
);

// ------------------------------------------------------------
// COMPILE
// ------------------------------------------------------------

console.log(
    "\n[2] COMPILE → BYV BINARY"
);

const compiled =
    byv.compile(
        source
    );

console.log(
    "Compiled size:",
    compiled.length,
    "bytes"
);

// ------------------------------------------------------------
// INSPECT
// ------------------------------------------------------------

console.log(
    "\n[3] INSPECT"
);

console.dir(
    byv.inspect(
        compiled
    ),
    { depth: null }
);

// ------------------------------------------------------------
// NORMAL DECODE
// ------------------------------------------------------------

console.log(
    "\n[4] NORMAL DESERIALIZE"
);

const normal =
    byv.deserialize(
        compiled
    );

console.dir(
    normal,
    { depth: null }
);

// ------------------------------------------------------------
// FAST DECODE
// ------------------------------------------------------------

console.log(
    "\n[5] FAST DESERIALIZE"
);

const fast =
    byv.deserializeFast(
        compiled
    );

console.dir(
    fast,
    { depth: null }
);

// ------------------------------------------------------------
// CORRECTNESS
// ------------------------------------------------------------

console.log(
    "\n[6] CORRECTNESS"
);

const parsedJSON =
    JSON.stringify(
        parsed
    );

const normalJSON =
    JSON.stringify(
        normal
    );

const fastJSON =
    JSON.stringify(
        fast
    );

const normalMatch =
    parsedJSON ===
    normalJSON;

const fastMatch =
    parsedJSON ===
    fastJSON;

console.log(
    "PARSE → NORMAL:",
    normalMatch
        ? "✓ MATCH"
        : "✗ MISMATCH"
);

console.log(
    "PARSE → FAST  :",
    fastMatch
        ? "✓ MATCH"
        : "✗ MISMATCH"
);

if (
    !normalMatch ||
    !fastMatch
) {
    throw new Error(
        "BYV Language correctness check failed"
    );
}

// ------------------------------------------------------------
// SIZE
// ------------------------------------------------------------

console.log(
    "\n[7] SIZE COMPARISON"
);

const jsonSize =
    Buffer.byteLength(
        JSON.stringify(
            parsed
        ),
        "utf8"
    );

const byvSize =
    compiled.length;

const ratio =
    (byvSize / jsonSize) *
    100;

const saving =
    100 - ratio;

console.log(
    "JSON size:",
    jsonSize,
    "bytes"
);

console.log(
    "BYV size :",
    byvSize,
    "bytes"
);

console.log(
    "BYV/JSON :",
    ratio.toFixed(2) + "%"
);

console.log(
    "Saving   :",
    saving.toFixed(2) + "%"
);

console.log(
    "\n✓ BYV LANGUAGE END-TO-END PASS"
);