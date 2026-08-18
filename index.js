import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";
import path from "node:path";
import load from "node-gyp-build";

const require = createRequire(
    import.meta.url);

const root = path.dirname(
    fileURLToPath(
        import.meta.url)
);

const byv = load(root);

export default byv;

export const version =
    byv.version;

export const parse =
    byv.parse;

export const compile =
    byv.compile;

export const serialize =
    byv.serialize;

export const serializeFast =
    byv.serializeFast;

export const deserialize =
    byv.deserialize;

export const deserializeFast =
    byv.deserializeFast;

export const inspect =
    byv.inspect;