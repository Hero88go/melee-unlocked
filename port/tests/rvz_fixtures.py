"""Writes RVZ and WIA disc images for port/tests/disc_rvz_test.cpp.

A test-side encoder, written from Dolphin's own writer (Source/Core/DiscIO/WIABlob.cpp, RVZPack and
LaggedFibonacciGenerator::GetSeed, release 2506): the raw data region of a GameCube disc split
into chunks, each chunk RVZ-packed (literal runs plus junk runs found by GetSeed) and compressed.

  py -3.12 port/tests/rvz_fixtures.py fixtures            # the small fixtures in tests/data/rvz
  py -3.12 port/tests/rvz_fixtures.py encode in.iso out.rvz [--method zstd] [--chunk 131072] [--wia]

Needs the zstandard and numpy modules (pip install zstandard numpy); lzma and bz2 are in Python.
"""
import argparse
import bisect
import bz2
import hashlib
import lzma
import os
import random
import struct
import sys

import numpy as np

K, J, SEED = 521, 32, 17
BLOCK = 0x8000
METHODS = {"none": 0, "purge": 1, "bzip2": 2, "lzma": 3, "lzma2": 4, "zstd": 5}


# Dolphin's LaggedFibonacciGenerator. Words are kept in output (big-endian) order.
def _forward(b):
    for i in range(J):
        b[i] ^= b[i + K - J]
    for i in range(J, K):
        b[i] ^= b[i - J]


def _backward(b, start=0, end=K):
    for i in range(min(end, K), max(J, start), -1):
        b[i - 1] ^= b[i - 1 - J]
    for i in range(min(end, J), start, -1):
        b[i - 1] ^= b[i - 1 + K - J]


def _initialize(b, check):
    for i in range(SEED, K):
        calc = ((b[i - 17] << 23) & 0xFFFFFFFF) ^ (b[i - 16] >> 9) ^ b[i - 1]
        if check:
            actual = (b[i] & 0xFF00FFFF) | ((b[i] << 2) & 0x00FC0000)
            if (calc & 0xFFFCFFFF) != actual:
                return False
        b[i] = calc
    for i in range(K):
        x = b[i]
        b[i] = (x & 0xFF00FFFF) | ((x >> 2) & 0x00FF0000)
    for _ in range(4):
        _forward(b)
    return True


def lfg_state(seed_words):
    b = list(seed_words) + [0] * (K - SEED)
    _initialize(b, False)
    return b


def lfg_bytes(seed_words, skip, count):
    """count bytes of junk for a seed, starting `skip` bytes into the stream."""
    b = lfg_state(seed_words)
    out = bytearray()
    pos = skip
    while pos >= K * 4:
        _forward(b)
        pos -= K * 4
    while len(out) < count:
        out += struct.pack(">%dI" % K, *b)[pos:]
        pos = 0
        _forward(b)
    return bytes(out[:count])


def get_seed(data, data_offset):
    """Dolphin's GetSeed: (seed words, bytes it regenerates) for junk at data_offset within a block."""
    skip = (-data_offset) % 4
    words_n = (len(data) - skip) // 4
    if words_n < K:
        return None, 0
    w = struct.unpack_from(">%dI" % K, data, skip)
    if not all((x & 0x00C00000) == ((x >> 2) & 0x00C00000) for x in w):
        return None, 0
    word_off = (data_offset + skip) // 4
    mod, div = word_off % K, word_off // K
    b = [0] * K
    b[mod:K] = w[0:K - mod]
    b[0:mod] = w[K - mod:K]
    _backward(b, 0, mod)
    for _ in range(div):
        _backward(b)
    for _ in range(4):
        _backward(b)
    for i in range(SEED):
        b[i] = (b[i] & 0xFF00FFFF) | ((b[i] << 2) & 0x00FC0000) | (((b[i + 16] ^ b[i + 15]) << 9) & 0x00030000)
    seed = b[:SEED]
    if not _initialize(b, True):
        return None, 0
    gen = np.frombuffer(lfg_bytes(seed, data_offset, len(data)), dtype=np.uint8)
    have = np.frombuffer(data, dtype=np.uint8)
    diff = np.nonzero(gen != have)[0]
    return seed, int(diff[0]) if len(diff) else len(data)


def find_junk(disc, start, end):
    """Junk runs in disc[start:end] as {end: (start, seed)}, positions relative to start (RVZPack)."""
    junk = {}
    pos, total = 0, end - start
    view = memoryview(disc)
    arr = np.frombuffer(disc, dtype=np.uint8)
    while pos < total:
        # Skip zero bytes (RVZPack does it byte by byte; this finds the next non-zero byte).
        while pos < total:
            window = arr[start + pos:start + min(total, pos + (1 << 20))]
            nz = np.flatnonzero(window)
            if len(nz):
                pos += int(nz[0])
                break
            pos += len(window)
        if pos >= total:
            break
        off = pos   # region offset; GameCube's region starts at 0
        n = min(((off // BLOCK) + 1) * BLOCK - off, total - pos)
        seed, got = get_seed(bytes(view[start + pos:start + pos + n]), off % BLOCK)
        if got > 0:
            junk[pos + got] = (pos, seed)
        pos += n
    return junk


def rvz_pack(chunk_bytes, chunk_off, junk_ends, junk):
    """RVZPack for one chunk; None when packing would not help (stored plain)."""
    out = bytearray()
    cur, end = 0, len(chunk_bytes)
    first = True
    while cur < end:
        ns, ne, seed = end, end, None
        if end - cur > SEED * 4:
            i = bisect.bisect_right(junk_ends, chunk_off + cur + SEED * 4)
            if i < len(junk_ends):
                jend = junk_ends[i]
                jstart, jseed = junk[jend]
                if jstart + SEED * 4 < chunk_off + end:
                    ns = max(cur, jstart - chunk_off)
                    ne = min(end, jend - chunk_off)
                    seed = jseed
        if first:
            if ns == end:
                return None
            first = False
        if ns > cur:
            out += struct.pack(">I", ns - cur) + chunk_bytes[cur:ns]
            cur = ns
        if ne > cur:
            out += struct.pack(">I", (ne - cur) | 0x80000000) + struct.pack(">17I", *seed)
            cur = ne
    return bytes(out)


LZMA1 = {"id": lzma.FILTER_LZMA1, "dict_size": 1 << 20, "lc": 3, "lp": 0, "pb": 2}
LZMA2 = {"id": lzma.FILTER_LZMA2, "dict_size": 1 << 20}


def compressor_data(method):
    if method == "lzma":
        return bytes([(2 * 5 + 0) * 9 + 3]) + struct.pack("<I", 1 << 20)
    if method == "lzma2":
        return bytes([16])   # (2 | 0) << (16 / 2 + 11) = 1 MiB
    return b""


def purge(data):
    # Segments of the non-zero data, then a SHA-1 of everything stored.
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        while i < n and data[i] == 0:
            i += 1
        if i >= n:
            break
        j = i
        while j < n:
            if data[j] == 0 and data[j:j + 64] == bytes(min(64, n - j)):
                break
            j += 1
        out += struct.pack(">II", i, j - i) + data[i:j]
        i = j
    return bytes(out) + hashlib.sha1(bytes(out)).digest()


def compress(method, data):
    if method == "none":
        return data
    if method == "purge":
        return purge(data)
    if method == "bzip2":
        return bz2.compress(data, 9)
    if method == "lzma":
        return lzma.compress(data, format=lzma.FORMAT_RAW, filters=[LZMA1])
    if method == "lzma2":
        return lzma.compress(data, format=lzma.FORMAT_RAW, filters=[LZMA2])
    if method == "zstd":
        import zstandard
        return zstandard.ZstdCompressor(level=5).compress(data)
    raise ValueError(method)


def encode(disc, path, method="zstd", chunk=0x20000, wia=False, pack=True, plain_groups=False, junk=None):
    rvz = not wia
    if rvz and method == "purge":
        raise ValueError("RVZ has no purge")
    iso_size = len(disc)
    n_groups = (iso_size + chunk - 1) // chunk
    if junk is None:
        junk = find_junk(disc, 0, iso_size) if (rvz and pack) else {}
    junk_ends = sorted(junk)
    groups, data = [], bytearray()
    head_len = 0x48 + 0xDC
    raw_table = struct.pack(">QQII", 0x80, iso_size - 0x80, 0, n_groups)
    raw_c = compress(method, raw_table)
    gsize = 12 if rvz else 8
    # The group table's stored size is known only after the groups are written; it goes after the data.
    data_start = (head_len + len(raw_c) + 3) & ~3
    for g in range(n_groups):
        start = g * chunk
        part = bytes(disc[start:min(start + chunk, iso_size)])
        packed = rvz_pack(part, start, junk_ends, junk) if (rvz and pack) else None
        payload = packed if packed is not None else part
        if not any(part):
            groups.append((0, 0, 0))
            continue
        if rvz and (plain_groups or method == "none"):
            stored, flag = payload, 0
        else:
            stored, flag = compress(method, payload), (0x80000000 if rvz else 0)
        while (data_start + len(data)) % 4:
            data.append(0)
        groups.append(((data_start + len(data)) >> 2, len(stored) | flag, len(packed) if packed is not None else 0))
        data += stored
    group_table = b"".join(struct.pack(">III", *e) if rvz else struct.pack(">II", *e[:2]) for e in groups)
    group_c = compress(method, group_table)
    while (data_start + len(data)) % 4:
        data.append(0)
    group_off = data_start + len(data)
    cd = compressor_data(method)
    disc_struct = struct.pack(">IIiI", 1, METHODS[method], 5, chunk) + bytes(disc[:0x80])
    disc_struct += struct.pack(">IIQ", 0, 0x30, 0) + bytes(20)
    disc_struct += struct.pack(">IQI", 1, head_len, len(raw_c))
    disc_struct += struct.pack(">IQI", len(groups), group_off, len(group_c))
    disc_struct += bytes([len(cd)]) + cd.ljust(7, b"\0")
    assert len(disc_struct) == 0xDC
    file_size = group_off + len(group_c)
    head = (b"RVZ\x01" if rvz else b"WIA\x01") + struct.pack(">III", 0x01000000, 0x00030000 if rvz else 0x00090000, 0xDC)
    head += hashlib.sha1(disc_struct).digest() + struct.pack(">QQ", iso_size, file_size)
    head += hashlib.sha1(head).digest()
    assert len(head) == 0x48
    body = head + disc_struct + raw_c
    body += bytes(data_start - len(body))
    with open(path, "wb") as f:
        f.write(body)
        f.write(data)
        f.write(group_c)
    return len(junk)


def synthetic_disc():
    """A small GameCube-like disc: header, text, zero runs, LFG junk (block-aligned and not), random bytes."""
    rng = random.Random(1102)
    size = 5 * BLOCK + 12345
    disc = bytearray(size)
    disc[0:8] = b"GTST01\x00\x02"
    disc[0x20:0x40] = b"Melee Unlocked RVZ test disc".ljust(32, b"\0")
    disc[0x1C:0x20] = bytes.fromhex("C2339F3D")
    disc[0x400:0x2400] = bytes(rng.randrange(256) for _ in range(0x2000))
    # Nintendo's junk restarts from a new seed at every 0x8000 block and continues the stream
    # (skipped to the offset in the block) when it starts after a file. Three runs:
    # a whole block less 100 bytes, one from an odd offset after a file to the block's end, and one
    # from the next block's start into it.
    junk = {}
    def put(start, end):
        seed = [rng.getrandbits(32) for _ in range(SEED)]
        disc[start:end] = lfg_bytes(seed, start % BLOCK, end - start)
        junk[end] = (start, seed)
    put(BLOCK, 2 * BLOCK - 100)
    disc[2 * BLOCK:2 * BLOCK + 0x1234] = b"FILEDATA" * (0x1234 // 8) + b"FILE"
    put(2 * BLOCK + 0x1234 + 33, 3 * BLOCK)          # after the file and 33 zero bytes
    put(3 * BLOCK, 3 * BLOCK + 0x4000)
    disc[4 * BLOCK + 5:4 * BLOCK + 3000] = bytes(rng.randrange(256) for _ in range(2995))
    disc[size - 64:size] = b"END!" * 16
    # Dolphin's GetSeed (ported above) must give back every run from the disc bytes alone.
    for end, (start, seed) in junk.items():
        got_seed, got = get_seed(bytes(disc[start:((start // BLOCK) + 1) * BLOCK]), start % BLOCK)
        assert got == end - start, ("GetSeed", start, got, end - start)
        junk[end] = (start, got_seed)
    return bytes(disc), junk


FIXTURES = [
    ("rvz-zstd.rvz", dict(method="zstd")),
    ("rvz-zstd-unpacked.rvz", dict(method="zstd", pack=False)),
    ("rvz-plain-groups.rvz", dict(method="zstd", plain_groups=True)),
    ("rvz-none.rvz", dict(method="none")),
    ("rvz-bzip2.rvz", dict(method="bzip2")),
    ("rvz-lzma.rvz", dict(method="lzma")),
    ("rvz-lzma2.rvz", dict(method="lzma2")),
    ("wia-none.wia", dict(method="none", wia=True)),
    ("wia-purge.wia", dict(method="purge", wia=True)),
    ("wia-lzma2.wia", dict(method="lzma2", wia=True)),
]


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("fixtures")
    e = sub.add_parser("encode")
    e.add_argument("iso")
    e.add_argument("out")
    e.add_argument("--method", default="zstd", choices=sorted(METHODS))
    e.add_argument("--chunk", type=int, default=0x20000)
    e.add_argument("--wia", action="store_true")
    a = ap.parse_args()
    if a.cmd == "fixtures":
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "data", "rvz")
        os.makedirs(out, exist_ok=True)
        disc, junk = synthetic_disc()
        with open(os.path.join(out, "disc.iso"), "wb") as f:
            f.write(disc)
        found = find_junk(disc, 0, len(disc))
        print("junk runs:", sorted((s, e) for e, (s, _) in junk.items()),
              "found without file positions:", sorted((s, e) for e, (s, _) in found.items()))
        for name, opts in FIXTURES:
            chunk = 0x8000 if "zstd" in name else 0x10000
            encode(disc, os.path.join(out, name), chunk=chunk, junk=junk if not opts.get("wia") else {}, **opts)
            print(name, os.path.getsize(os.path.join(out, name)))
        return
    with open(a.iso, "rb") as f:
        disc = f.read()
    runs = encode(disc, a.out, method=a.method, chunk=a.chunk, wia=a.wia)
    print("wrote", a.out, os.path.getsize(a.out), "bytes,", runs, "junk runs")


if __name__ == "__main__":
    sys.exit(main())
