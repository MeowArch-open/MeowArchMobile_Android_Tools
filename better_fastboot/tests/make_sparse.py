#!/usr/bin/env python3
"""make_sparse.py — build an Android sparse image with a few huge raw chunks.

Usage: make_sparse.py OUT_PATH TOTAL_BYTES [CHUNKS]

The point is the *shape*, not the content: libsparse hands the flash driver one
callback per chunk, so a 64 MiB chunk used to become a single 64 MiB transport
write, and the progress callback — which only fires when a write returns — went
quiet for the whole chunk and then jumped. Small chunks hide the bug, large ones
expose it; CHUNKS=1 makes it maximal.
"""
import os
import struct
import sys

MAGIC = 0xED26FF3A
CHUNK_TYPE_RAW = 0xCAC1
BLOCK = 4096


def main() -> int:
    out = sys.argv[1]
    total = int(sys.argv[2])
    chunks = int(sys.argv[3]) if len(sys.argv) > 3 else 1

    total = (total + BLOCK - 1) // BLOCK * BLOCK
    per_chunk = (total // chunks + BLOCK - 1) // BLOCK * BLOCK
    blocks = per_chunk // BLOCK

    with open(out, "wb") as f:
        f.write(struct.pack("<IHHHHIIII", MAGIC, 0, 0, 28, 12, BLOCK,
                            (total // BLOCK), chunks, 0))
        payload = os.urandom(BLOCK)
        for _ in range(chunks):
            f.write(struct.pack("<HHII", CHUNK_TYPE_RAW, 0, blocks,
                                12 + per_chunk))
            for _ in range(per_chunk // BLOCK):
                f.write(payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())