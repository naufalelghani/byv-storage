import byv from "./native.cjs";

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