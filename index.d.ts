export interface ByvInspectResult {
    valid: boolean;
    version: number;
    byteLength: number;
    payloadSize?: number;
    flags?: number;
}

export interface ByvApi {
    version(): number;

    parse(source: string): unknown;

    compile(source: string): Buffer;

    serialize(value: unknown): Buffer;

    serializeFast(value: unknown): Buffer;

    deserialize(buffer: Buffer): unknown;

    deserializeFast(buffer: Buffer): unknown;

    inspect(
        buffer: Buffer
    ): ByvInspectResult;
}

declare const byv: ByvApi;

export default byv;

export declare const version:
    ByvApi["version"];

export declare const parse:
    ByvApi["parse"];

export declare const compile:
    ByvApi["compile"];

export declare const serialize:
    ByvApi["serialize"];

export declare const serializeFast:
    ByvApi["serializeFast"];

export declare const deserialize:
    ByvApi["deserialize"];

export declare const deserializeFast:
    ByvApi["deserializeFast"];

export declare const inspect:
    ByvApi["inspect"];