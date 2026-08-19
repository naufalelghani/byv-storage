const MAGIC = Buffer.from("BYV7", "ascii");

const u32 = (value) => {
    const buffer = Buffer.alloc(4);
    buffer.writeUInt32LE(value);
    return buffer;
};

export const legacy = {
    null() {
        return Buffer.from([0]);
    },

    bool(value) {
        return Buffer.from([1, value ? 1 : 0]);
    },

    int(value) {
        const buffer = Buffer.alloc(9);
        buffer[0] = 2;
        buffer.writeBigInt64LE(BigInt(value), 1);
        return buffer;
    },

    float(value) {
        const buffer = Buffer.alloc(9);
        buffer[0] = 3;
        buffer.writeDoubleLE(value, 1);
        return buffer;
    },

    str(value) {
        const bytes = Buffer.from(value, "utf8");
        return Buffer.concat([
            Buffer.from([4]),
            u32(bytes.length),
            bytes
        ]);
    },

    obj(entries) {
        const pairs = Array.isArray(entries)
            ? entries
            : Object.entries(entries);

        const values = [
            Buffer.from([5]),
            u32(pairs.length)
        ];

        for (const [key, value] of pairs) {
            const keyBytes = Buffer.from(key, "utf8");

            values.push(
                u32(keyBytes.length),
                keyBytes,
                value
            );
        }

        return Buffer.concat(values);
    },

    arr(values) {
        return Buffer.concat([
            Buffer.from([6]),
            u32(values.length),
            ...values
        ]);
    }
};

export function withHeader(flags, payload) {
    const header = Buffer.alloc(10);

    MAGIC.copy(header);
    header[4] = 7;
    header[5] = flags;
    header.writeUInt32LE(payload.length, 6);

    return Buffer.concat([header, payload]);
}
