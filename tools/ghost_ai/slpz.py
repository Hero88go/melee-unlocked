"""slpz -> slp (the format of github.com/AlexanderHarrison/slpz, version 0), in Python with numpy.

    py -3.12 slpz.py in.slpz out.slp
"""
import struct
import sys

import numpy as np
import zstandard

RAW_HEADER = bytes([0x7B, 0x55, 0x03, 0x72, 0x61, 0x77, 0x5B, 0x24, 0x55, 0x23, 0x6C])
_dctx = zstandard.ZstdDecompressor()


def _event_sizes(b):
    info = b[1]
    sizes = np.zeros(256, np.int64)
    for i in range((info - 1) // 3):
        o = i * 3 + 2
        sizes[b[o]] = struct.unpack(">H", b[o + 1:o + 3])[0]
    return sizes


def decompress(z: bytes) -> bytes:
    version, es_off, gs_off, md_off, ev_off, ev_size = struct.unpack(">6I", z[:24])
    if version > 0:
        raise ValueError("unknown slpz version")
    sizes = _event_sizes(z[es_off:gs_off])
    b = np.frombuffer(_dctx.decompress(z[ev_off:], max_output_size=ev_size), np.uint8)
    total = int(struct.unpack(">I", b[:4].tobytes())[0])
    order = b[4:4 + total]
    data = b[4 + total:]
    counts = np.bincount(order, minlength=256)
    offsets = np.zeros(257, np.int64)
    offsets[1:] = np.cumsum(sizes * counts)
    out_len = int(offsets[256]) + total
    out = np.empty(out_len, np.uint8)
    # Where each event starts in the output.
    ev_len = 1 + sizes[order]
    starts = np.zeros(total, np.int64)
    starts[1:] = np.cumsum(ev_len)[:-1]
    out[starts] = order
    for e in np.nonzero(counts)[0]:
        n, sz = int(counts[e]), int(sizes[e])
        if sz == 0:
            continue
        cols = data[offsets[e]:offsets[e] + n * sz].reshape(sz, n).T   # (n events, sz bytes)
        pos = starts[order == e]
        out[(pos[:, None] + 1 + np.arange(sz)[None]).ravel()] = cols.ravel()
    raw = z[es_off:gs_off] + z[gs_off:md_off] + out.tobytes()
    return RAW_HEADER + struct.pack(">I", len(raw)) + raw + z[md_off:ev_off]


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        slp = decompress(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(slp)
