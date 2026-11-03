#!/usr/bin/env python3
"""Merge shader caches from several seed-generation runs into one shader seed (renderer_gl.cpp seed_cache).

Usage: seed-merge.py <out-dir> <cache-dir> [<cache-dir> ...]
Each cache dir holds gx-gl410-v2-s1/{vs-*.glsl, ps-*.glsl, recipes.bin}. Shaders merge by file name (the name is
the shader's hash); recipes merge by their exact bytes, in first-seen order, in the same file format the renderer
writes (magic, count, hash of the records, then the records). Prints what it found, and refuses a file that is not
plain GLSL text so nothing else can ride along into the bundle.
"""
import os
import shutil
import struct
import sys

MAGIC = 0x3150524C47505847  # "GXPGLRP1"
SUBDIR = "gx-gl410-v2-s1"


def fnv1a64(data: bytes) -> int:
    """gx::hash_bytes (port/runtime/gx/gx_texture.cpp): 8-byte little-endian words, then the tail bytes.
    Checked against every file the renderer wrote before any merged file is trusted."""
    m = 0xFFFFFFFFFFFFFFFF
    h = 0xCBF29CE484222325
    whole = len(data) - len(data) % 8
    for (v,) in struct.iter_unpack("<Q", data[:whole]):
        h = ((h ^ v) * 0x100000001B3) & m
        h ^= h >> 29
    for b in data[whole:]:
        h = ((h ^ b) * 0x100000001B3) & m
    return h


def read_recipes(path):
    data = open(path, "rb").read()
    magic, count, digest = struct.unpack_from("<QQQ", data, 0)
    body = data[24:]
    if magic != MAGIC or count == 0 or len(body) % count:
        raise SystemExit(f"{path}: not a recipes file")
    size = len(body) // count
    return size, [body[i * size:(i + 1) * size] for i in range(count)], digest, body


def main():
    out, caches = sys.argv[1], sys.argv[2:]
    dest = os.path.join(out, SUBDIR)
    os.makedirs(dest, exist_ok=True)
    shaders, seen, recipes, size = 0, set(), [], None
    for cache in caches:
        src = os.path.join(cache, SUBDIR)
        for name in sorted(os.listdir(src)):
            path = os.path.join(src, name)
            if name.endswith(".glsl"):
                text = open(path, "rb").read()
                if not text.startswith(b"#version 410") or b"\0" in text:
                    raise SystemExit(f"{path}: not plain GLSL; refusing")
                if not os.path.exists(os.path.join(dest, name)):
                    shutil.copyfile(path, os.path.join(dest, name))
                    shaders += 1
            elif name == "recipes.bin":
                rsize, records, digest, body = read_recipes(path)
                if fnv1a64(body) != digest:
                    raise SystemExit(f"{path}: recipe hash differs from gx::hash_bytes as implemented here")
                if size not in (None, rsize):
                    raise SystemExit(f"{path}: recipe size {rsize} differs from {size}")
                size = rsize
                for r in records:
                    if r not in seen:
                        seen.add(r)
                        recipes.append(r)
    if recipes:
        body = b"".join(recipes)
        with open(os.path.join(dest, "recipes.bin"), "wb") as f:
            f.write(struct.pack("<QQQ", MAGIC, len(recipes), fnv1a64(body)) + body)
    total = len([n for n in os.listdir(dest) if n.endswith(".glsl")])
    print(f"seed: {total} shaders ({shaders} new from {len(caches)} caches), {len(recipes)} recipes of {size} bytes")


if __name__ == "__main__":
    main()
